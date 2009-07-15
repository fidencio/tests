/*
   SSSD

   Async resolver - SRV records parsing

   Authors:
        Jakub Hrozek <jhrozek@redhat.com>

   Copyright (C) Red Hat, Inc 2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * This code is based on other c-ares parsing licensed as follows:

 * Copyright 1998 by the Massachusetts Institute of Technology.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <stdlib.h>
#include <string.h>
#include "ares.h"
/* this drags in some private macros c-ares uses */
#include "ares_dns.h"

#include "ares_parse_srv_reply.h"

int _ares_parse_srv_reply (const unsigned char *abuf, int alen,
                           struct srv_reply **srv_out, int *nsrvreply)
{
  unsigned int qdcount, ancount;
  const unsigned char *aptr;
  int status, i, rr_type, rr_class, rr_len;
  long len;
  char *hostname = NULL, *rr_name = NULL;
  struct srv_reply *srv = NULL;

  /* Set *srv_out to NULL for all failure cases. */
  if (srv_out)
    *srv_out = NULL;
  /* Same with *nsrvreply. */
  if (nsrvreply)
    *nsrvreply = 0;

  /* Give up if abuf doesn't have room for a header. */
  if (alen < HFIXEDSZ)
    return ARES_EBADRESP;

  /* Fetch the question and answer count from the header. */
  qdcount = DNS_HEADER_QDCOUNT (abuf);
  ancount = DNS_HEADER_ANCOUNT (abuf);
  if (qdcount != 1)
    return ARES_EBADRESP;
  if (ancount == 0)
    return ARES_ENODATA;

  /* Expand the name from the question, and skip past the question. */
  aptr = abuf + HFIXEDSZ;
  status = ares_expand_name (aptr, abuf, alen, &hostname, &len);
  if (status != ARES_SUCCESS)
    return status;

  if (aptr + len + QFIXEDSZ > abuf + alen)
    {
      free (hostname);
      return ARES_EBADRESP;
    }
  aptr += len + QFIXEDSZ;

  /* Allocate srv_reply array; ancount gives an upper bound */
  srv = malloc ((ancount) * sizeof (struct srv_reply));
  if (!srv)
    {
      free (hostname);
      return ARES_ENOMEM;
    }

  /* Examine each answer resource record (RR) in turn. */
  for (i = 0; i < (int) ancount; i++)
    {
      /* Decode the RR up to the data field. */
      status = ares_expand_name (aptr, abuf, alen, &rr_name, &len);
      if (status != ARES_SUCCESS)
        {
          break;
        }
      aptr += len;
      if (aptr + RRFIXEDSZ > abuf + alen)
        {
          status = ARES_EBADRESP;
          break;
        }
      rr_type = DNS_RR_TYPE (aptr);
      rr_class = DNS_RR_CLASS (aptr);
      rr_len = DNS_RR_LEN (aptr);
      aptr += RRFIXEDSZ;

      /* Check if we are really looking at a SRV record */
      if (rr_class == C_IN && rr_type == T_SRV)
        {
          /* parse the SRV record itself */
          if (rr_len < 6)
            {
              status = ARES_EBADRESP;
              break;
            }

          srv[i].priority = ntohs (*((const uint16_t *)aptr));
          aptr += sizeof(uint16_t);
          srv[i].weight = ntohs (*((const uint16_t *)aptr));
          aptr += sizeof(uint16_t);
          srv[i].port = ntohs (*((const uint16_t *)aptr));
          aptr += sizeof(uint16_t);

          status = ares_expand_name (aptr, abuf, alen, &srv[i].host, &len);
          if (status != ARES_SUCCESS)
            break;

          /* Move on to the next record */
          aptr += len;

          /* Don't lose memory in the next iteration */
          free (rr_name);
          rr_name = NULL;
        }
    }

  /* clean up on error */
  if (status != ARES_SUCCESS)
    {
      free (srv);
      free (hostname);
      free (rr_name);
      return status;
    }

  /* everything looks fine, return the data */
  *srv_out = srv;
  *nsrvreply = ancount;

  free (hostname);
  free (rr_name);
  return status;
}
