/*
 *  tvheadend, UPnP MediaServer
 *  Copyright (C) 2015 Damjan Marion
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

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "tvheadend.h"
#include "tvhpoll.h"
#include "upnp.h"
#include "upnp_mediaserver.h"

static upnp_service_t *upnp_mediaserver;

static void
upnp_mediaserver_received(uint8_t *data, size_t len, udp_connection_t *conn,
                          struct sockaddr_storage *storage)
{
  char *buf, *ptr, *saveptr;
  char *argv[10];
  char *st = NULL, *man = NULL;

  if (len < 32 || len > 8191)
    return;

#define MSEARCH "M-SEARCH * HTTP/1.1"

  if (strncmp((char *)data, MSEARCH, sizeof(MSEARCH)-1))
    return;

#undef MSEARCH

  buf = alloca(len+1);
  memcpy(buf, data, len);
  buf[len] = '\0';
  ptr = strtok_r(buf, "\r\n", &saveptr);
  /* Request decoder */
  if (ptr) {
    if (http_tokenize(ptr, argv, 3, -1) != 3)
      return;
    if (conn->multicast) {
      if (strcmp(argv[0], "M-SEARCH"))
        return;
      if (strcmp(argv[1], "*"))
        return;
      if (strcmp(argv[2], "HTTP/1.1"))
        return;
    } else {
      if (strcmp(argv[0], "HTTP/1.1"))
        return;
      if (strcmp(argv[1], "200"))
        return;
    }
    ptr = strtok_r(NULL, "\r\n", &saveptr);
  }
  /* Header decoder */
  while (1) {
    if (ptr == NULL)
      break;
    if (http_tokenize(ptr, argv, 2, ':') == 2) {
      if (strcmp(argv[0], "ST") == 0)
        st = argv[1];
      else if (strcasecmp(argv[0], "MAN") == 0)
        man = argv[1];
    }
    ptr = strtok_r(NULL, "\r\n", &saveptr);
  }
  if (st == NULL || strcmp(st, "urn:schemas-upnp-org:device:MediaServer:1"))
    return;
  if (man == NULL || strcmp(man, "\"ssdp:discover\""))
    return;

	tvhtrace("upnp-mediaserver", "received %d bytes of data",  (int) len);
	tvhlog_hexdump("upnp-mediaserver", data, len);
	printf("%s\n", data);

}

static void
upnp_mediaserver_destroy(upnp_service_t *upnp)
{
  upnp_mediaserver = NULL;
}

/*
 *  Fire up UPnP MediaServer
 */
void
upnp_mediaserver_init(void)
{
  tvhlog(LOG_INFO, "upnp-mediaserver", "service started");

  upnp_mediaserver = upnp_service_create(upnp_service);
  if (upnp_mediaserver == NULL) {
    tvherror("upnp-mediaserver", "unable to create UPnP MediaServer service");
  } else {
    upnp_mediaserver->us_received = upnp_mediaserver_received;
    upnp_mediaserver->us_destroy  = upnp_mediaserver_destroy;
  }

}

void
upnp_mediaserver_done(void)
{

}

