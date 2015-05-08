/*
 *  tvheadend, DVB CAM interface
 *  Copyright (C) 2014 Damjan Marion
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include "tvheadend.h"
#include "caclient.h"
#include "service.h"
#include "input.h"

#include "dvbcam.h"
#include "input/mpegts/linuxdvb/linuxdvb_private.h"

#if ENABLE_LINUXDVB_CA

#define CAIDS_PER_CA_SLOT	16

typedef struct dvbcam_active_service {
  TAILQ_ENTRY(dvbcam_active_service) link;
  service_t            *t;
  uint8_t              *last_pmt;
  int                  last_pmt_len;
  linuxdvb_ca_t        *ca;
  uint8_t              slot;
} dvbcam_active_service_t;

typedef struct dvbcam_active_cam {
  TAILQ_ENTRY(dvbcam_active_cam) link;
  uint16_t             caids[CAIDS_PER_CA_SLOT];
  int                  num_caids;
  linuxdvb_ca_t       *ca;
  uint8_t              slot;
  int                  active_programs;
  int                  max_programs;
  int                  send_all;
} dvbcam_active_cam_t;

TAILQ_HEAD(,dvbcam_active_service) dvbcam_active_services;
TAILQ_HEAD(,dvbcam_active_cam) dvbcam_active_cams;

pthread_mutex_t dvbcam_mutex;

static void
dvbcam_dump_data()
{
  dvbcam_active_cam_t *ac;
  dvbcam_active_service_t *as;

  tvhtrace("dvbcam", "active services:");

  TAILQ_FOREACH(as, &dvbcam_active_services, link)
    tvhtrace("dvbcam", "slot %u service %p last_pmt %p last_pmt_len %u ca %p ",
             as->slot, as->t, as->last_pmt, as->last_pmt_len, as->ca );

  tvhtrace("dvbcam", "active cams:");

  TAILQ_FOREACH(ac, &dvbcam_active_cams, link)
    tvhtrace("dvbcam", "num_caids %u ca %p slot %u active_prog %d max_prog %d",
             ac->num_caids, ac->ca, ac->slot, ac->active_programs,
             ac->max_programs);
}

void
dvbcam_register_cam(linuxdvb_ca_t * lca, uint8_t slot, uint16_t * caids,
                    int num_caids, int max_programs)
{
  dvbcam_active_cam_t *ac;

  tvhtrace("dvbcam", "register cam ca %p slot %u num_caids %u max_programs %u",
           lca, slot, num_caids, max_programs);

  num_caids = MIN(CAIDS_PER_CA_SLOT, num_caids);

  if ((ac = malloc(sizeof(*ac))) == NULL)
  	return;

  ac->ca = lca;
  ac->slot = slot;
  memcpy(ac->caids, caids, num_caids * sizeof(uint16_t));
  ac->num_caids = num_caids;
  ac->max_programs = max_programs;

  pthread_mutex_lock(&dvbcam_mutex);

  TAILQ_INSERT_TAIL(&dvbcam_active_cams, ac, link);

  dvbcam_dump_data();
  pthread_mutex_unlock(&dvbcam_mutex);
}

void
dvbcam_unregister_cam(linuxdvb_ca_t * lca, uint8_t slot)
{
  dvbcam_active_cam_t *ac, *ac_tmp;
  dvbcam_active_service_t *as;

  tvhtrace("dvbcam", "unregister cam lca %p slot %u", lca, slot);

  pthread_mutex_lock(&dvbcam_mutex);

  /* remove pointer to this CAM in all active services */
  TAILQ_FOREACH(as, &dvbcam_active_services, link)
    if (as->ca == lca && as->slot == slot)
      as->ca = NULL;

  /* delete entry */
  for (ac = TAILQ_FIRST(&dvbcam_active_cams); ac != NULL; ac = ac_tmp) {
    ac_tmp = TAILQ_NEXT(ac, link);
    if(ac && ac->ca == lca && ac->slot == slot) {
      TAILQ_REMOVE(&dvbcam_active_cams, ac, link);
      free(ac);
    }
  }

  dvbcam_dump_data();
  pthread_mutex_unlock(&dvbcam_mutex);
}

void
dvbcam_pmt_data(mpegts_service_t *s, const uint8_t *ptr, int len)
{
  linuxdvb_frontend_t *lfe;
  dvbcam_active_cam_t *ac = NULL, *ac2;
  dvbcam_active_service_t *as = NULL, *as2;
  elementary_stream_t *st;
  caid_t *c;
//  int num_programs;
  int is_update = 0;
  int i;

	lfe = (linuxdvb_frontend_t*) s->s_dvb_active_input;

  if (!lfe)
    return;

  pthread_mutex_lock(&dvbcam_mutex);

  /* find this service in the list of active services */
  TAILQ_FOREACH(as2, &dvbcam_active_services, link)
    if (as2->t == (service_t *) s) {
      as = as2;
      break;
    }

  if (!as) {
    tvhtrace("dvbcam", "cannot find active service entry");
  	goto done;
  }

  if(as->last_pmt) {
    free(as->last_pmt);
    is_update = 1;
  }

  as->last_pmt = malloc(len + 3);
  memcpy(as->last_pmt, ptr-3, len + 3);
  as->last_pmt_len = len + 3;

  /*if this is update just send updated CAPMT to CAM */
  if (is_update) {
    linuxdvb_ca_send_capmt(as->ca, as->slot, as->last_pmt, as->last_pmt_len,
                           CA_LIST_MANAGEMENT_UPDATE, CA_PMT_CMD_ID_OK_DESCRAMBLING);
    tvhtrace("dvbcam", "CAPMT sent to CAM (update)");
    goto done;
  }

  as->ca = NULL;

  /* check all ellementary streams for CAIDs and find CAM */
  TAILQ_FOREACH(st, &s->s_components, es_link) {
    LIST_FOREACH(c, &st->es_caids, link) {
      TAILQ_FOREACH(ac2, &dvbcam_active_cams, link) {
        for(i=0;i<ac2->num_caids;i++) {
          if(ac2->ca && ac2->ca->lca_adapter == lfe->lfe_adapter &&
             ac2->caids[i] == c->caid)
          {
            as->ca = ac2->ca;
            as->slot = ac2->slot;
            ac = ac2;
            goto end_of_search_for_cam;
          }
        }
      }
    }
  }

end_of_search_for_cam:

  if (!ac) {
    tvhtrace("dvbcam", "cannot find active cam entry");
    goto done;
  }

#if 1
  /* count number of services in the list assigned to this specific CAM */
  int num_programs = 0;
  TAILQ_FOREACH(as2, &dvbcam_active_services, link)
    if(as2->ca == as->ca && as2->slot == as->slot)
      num_programs++;

  tvhtrace("dvbcam", "found %u active programs", num_programs);

  if (num_programs == 1)
  {
    linuxdvb_ca_send_capmt(as->ca, as->slot, as->last_pmt, as->last_pmt_len,
                           CA_LIST_MANAGEMENT_ONLY,  CA_PMT_CMD_ID_OK_DESCRAMBLING);
    ac->active_programs = 1;
    ac->send_all = 0;
    tvhtrace("dvbcam", "CAPMT sent to CAM (only)");
  }
  else if ((ac->active_programs == num_programs - 1) && !ac->send_all)
  {
    /* this is addition, send only new CAPMT */
    if (num_programs <= ac->max_programs) {
      linuxdvb_ca_send_capmt(as->ca, as->slot, as->last_pmt, as->last_pmt_len,
                             CA_LIST_MANAGEMENT_ADD, CA_PMT_CMD_ID_OK_DESCRAMBLING);
      ac->active_programs++;
      tvhtrace("dvbcam", "CAPMT sent to CAM (add)");
    }
  }
  else
  {
    /* re-send all CAPMTs */
    i = 0;
    TAILQ_FOREACH(as2, &dvbcam_active_services, link)
      if (as2->ca == as->ca && as2->slot == as->slot) {
        uint8_t list_mgmt;
        const char *list_mgmt_str;
        if (i == 0)
        {
          list_mgmt = CA_LIST_MANAGEMENT_FIRST;
          list_mgmt_str = "first";
        }
        else if (i + 1 == num_programs || i + 1  == ac->max_programs )
        {
          list_mgmt = CA_LIST_MANAGEMENT_LAST;
          list_mgmt_str = "last";
        }
        else
        {
          list_mgmt = CA_LIST_MANAGEMENT_MORE;
          list_mgmt_str = "more";
        }
        linuxdvb_ca_send_capmt(as2->ca, as2->slot, as2->last_pmt,
                               as2->last_pmt_len, list_mgmt, CA_PMT_CMD_ID_OK_DESCRAMBLING);
        i++;

        tvhtrace("dvbcam", "CAPMT sent to CAM (%s) %u / %u", list_mgmt_str, i,
                 num_programs);

        if (i == ac->max_programs)
          break;
      }
    ac->active_programs = i;
    ac->send_all = 0;
  }
#else
  /* re-send all CAPMTs */
  i = 0;
  TAILQ_FOREACH(as2, &dvbcam_active_services, link)
    if (as2->ca == as->ca && as2->slot == as->slot) {
      uint8_t list_mgmt;
      const char *list_mgmt_str;
      if (i == 0)
      {
        list_mgmt = CA_LIST_MANAGEMENT_FIRST;
        list_mgmt_str = "first";
      }
//      else if (i + 1 == num_programs || i + 1  == ac->max_programs )
//      {
//        list_mgmt = CA_LIST_MANAGEMENT_LAST;
//        list_mgmt_str = "last";
//      }
      else
      {
        list_mgmt = CA_LIST_MANAGEMENT_LAST;
        list_mgmt_str = "last";
      }
      linuxdvb_ca_send_capmt(as2->ca, as2->slot, as2->last_pmt,
                             as2->last_pmt_len, list_mgmt, CA_PMT_CMD_ID_OK_DESCRAMBLING);
      i++;

      tvhtrace("dvbcam", "CAPMT sent to CAM (%s)", list_mgmt_str);
    }

#endif
done:
  dvbcam_dump_data();
  pthread_mutex_unlock(&dvbcam_mutex);
}

void
dvbcam_service_start(service_t *t)
{
  dvbcam_active_service_t *as;

  tvhtrace("dvbcam", "start service %p", t);

  TAILQ_FOREACH(as, &dvbcam_active_services, link)
    if (as->t == t)
      return;

  if ((as = malloc(sizeof(*as))) == NULL)
    return;

  as->t = t;
  as->last_pmt = NULL;
  as->last_pmt_len = 0;

  pthread_mutex_lock(&dvbcam_mutex);
  TAILQ_INSERT_TAIL(&dvbcam_active_services, as, link);
  dvbcam_dump_data();
  pthread_mutex_unlock(&dvbcam_mutex);
}

void
dvbcam_service_stop(service_t *t)
{
	dvbcam_active_service_t *as, *as_tmp;
  linuxdvb_ca_t *ca = NULL;
  dvbcam_active_cam_t *ac2;
  uint8_t slot;

  tvhtrace("dvbcam", "stop service %p", t);

  pthread_mutex_lock(&dvbcam_mutex);

  for (as = TAILQ_FIRST(&dvbcam_active_services); as != NULL; as = as_tmp) {
    as_tmp = TAILQ_NEXT(as, link);
    if(as && as->t == t) {
      if(as->last_pmt) {
        linuxdvb_ca_send_capmt(as->ca, as->slot, as->last_pmt,
                             as->last_pmt_len, CA_LIST_MANAGEMENT_ADD, CA_PMT_CMD_ID_NOT_SELECTED);
        free(as->last_pmt);
      }
      slot = as->slot;
      ca = as->ca;
      TAILQ_REMOVE(&dvbcam_active_services, as, link);
      free(as);
    }
  }

  if (ca)
    TAILQ_FOREACH(ac2, &dvbcam_active_cams, link)
      if (ac2->slot == slot && ac2->ca == ca) {
        ac2->active_programs--;
        ac2->send_all = 1;
      }

  dvbcam_dump_data();
  pthread_mutex_unlock(&dvbcam_mutex);
}

void
dvbcam_init(void)
{
  pthread_mutex_init(&dvbcam_mutex, NULL);
  TAILQ_INIT(&dvbcam_active_services);
  TAILQ_INIT(&dvbcam_active_cams);
}

#endif /* ENABLE_LINUXDVB_CA */
