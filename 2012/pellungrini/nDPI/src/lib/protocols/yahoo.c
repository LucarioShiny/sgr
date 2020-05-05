/*
 * yahoo.c
 * Copyright (C) 2009-2011 by ipoque GmbH
 * 
 * This file is part of OpenDPI, an open source deep packet inspection
 * library based on the PACE technology by ipoque GmbH
 * 
 * OpenDPI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * OpenDPI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with OpenDPI.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */


#include "ndpi_utils.h"

#ifdef NDPI_PROTOCOL_YAHOO


struct ndpi_yahoo_header {
  u_int8_t YMSG_str[4];
  u_int16_t version;
  u_int16_t nothing0;
  u_int16_t len;
  u_int16_t service;
  u_int32_t status;
  u_int32_t session_id;
};

/* This function checks the pattern '<Ymsg Command=' in line 8 of parsed lines or
 * in the payload*/
static u_int8_t ndpi_check_for_YmsgCommand(u_int16_t len, const u_int8_t * ptr)
{
  u_int16_t i;

  for (i = 0; i < len - 12; i++) {
    if (ptr[i] == 'Y') {
      if (memcmp(&ptr[i + 1], "msg Command=", 12) == 0) {
	return 1;
      }
    }
  }
  return 0;
}

static void ndpi_int_yahoo_add_connection(struct ndpi_detection_module_struct *ndpi_struct, 
					  struct ndpi_flow_struct *flow, ndpi_protocol_type_t protocol_type)
{
  ndpi_int_add_connection(ndpi_struct, flow, NDPI_PROTOCOL_YAHOO, protocol_type);
}


	
#if !defined(WIN32)
static inline
#else
__forceinline static
#endif
u_int8_t check_ymsg(const u_int8_t * payload, u_int16_t payload_packet_len)
{

  const struct ndpi_yahoo_header *yahoo = (struct ndpi_yahoo_header *) payload;

  u_int16_t yahoo_len_parsed = 0;
  do {
    u_int16_t ylen = ntohs(yahoo->len);

    yahoo_len_parsed += 20 + ylen;	/* possible overflow here: 20 + ylen = 0x10000 --> 0 --> infinite loop */

    if (ylen >= payload_packet_len || yahoo_len_parsed >= payload_packet_len)
      break;

    yahoo = (struct ndpi_yahoo_header *) (payload + yahoo_len_parsed);
  }
  while (memcmp(yahoo->YMSG_str, "YMSG", 4) == 0);

  if (yahoo_len_parsed == payload_packet_len)
    return 1;
  return 0;
}

static void ndpi_search_yahoo_tcp(struct ndpi_detection_module_struct *ndpi_struct, struct ndpi_flow_struct *flow)
{
  struct ndpi_packet_struct *packet = &flow->packet;
	
  struct ndpi_id_struct *src = flow->src;
  struct ndpi_id_struct *dst = flow->dst;

  const struct ndpi_yahoo_header *yahoo = (struct ndpi_yahoo_header *) packet->payload;
  if (packet->payload_packet_len == 0) {
    return;
  }

  /* packet must be at least 20 bytes long */
  if (packet->payload_packet_len >= 20
      && memcmp(yahoo->YMSG_str, "YMSG", 4) == 0 && ((packet->payload_packet_len - 20) == ntohs(yahoo->len)
						     || check_ymsg(packet->payload, packet->payload_packet_len))) {
    NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE, "YAHOO FOUND\n");
    flow->yahoo_detection_finished = 2;
    if (ntohs(yahoo->service) == 24 || ntohs(yahoo->service) == 152 || ntohs(yahoo->service) == 74) {
      NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE, "YAHOO conference or chat invite  found");
      if (src != NULL) {
	src->yahoo_conf_logged_in = 1;
      }
      if (dst != NULL) {
	dst->yahoo_conf_logged_in = 1;
      }
    }
    if (ntohs(yahoo->service) == 27 || ntohs(yahoo->service) == 155 || ntohs(yahoo->service) == 160) {
      NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE, "YAHOO conference or chat logoff found");
      if (src != NULL) {
	src->yahoo_conf_logged_in = 0;
	src->yahoo_voice_conf_logged_in = 0;
      }
    }
    NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
    ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_REAL_PROTOCOL);
    return;
  } else if (flow->yahoo_detection_finished == 2 && packet->detected_protocol_stack[0] == NDPI_PROTOCOL_YAHOO) {
    return;
  } else if (packet->payload_packet_len == 4 && memcmp(yahoo->YMSG_str, "YMSG", 4) == 0) {
    flow->l4.tcp.yahoo_sip_comm = 1;
    return;
  } else if (flow->l4.tcp.yahoo_sip_comm && packet->detected_protocol_stack[0] == NDPI_PROTOCOL_UNKNOWN
	     && flow->packet_counter < 3) {
    return;
  }

  /* now test for http login, at least 100 a bytes packet */
  if (ndpi_struct->yahoo_detect_http_connections != 0 && packet->payload_packet_len > 100) {
    if (memcmp(packet->payload, "POST /relay?token=", 18) == 0
	|| memcmp(packet->payload, "GET /relay?token=", 17) == 0
	|| memcmp(packet->payload, "GET /?token=", 12) == 0
	|| memcmp(packet->payload, "HEAD /relay?token=", 18) == 0) {
      if ((src != NULL
	   && NDPI_COMPARE_PROTOCOL_TO_BITMASK(src->detected_protocol_bitmask, NDPI_PROTOCOL_YAHOO)
	   != 0) || (dst != NULL
		     && NDPI_COMPARE_PROTOCOL_TO_BITMASK(dst->detected_protocol_bitmask, NDPI_PROTOCOL_YAHOO)
		     != 0)) {
	/* this is mostly a file transfer */
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	return;
      }
    }
    if (memcmp(packet->payload, "POST ", 5) == 0) {
      u_int16_t a;
      ndpi_parse_packet_line_info(ndpi_struct, flow);

      if ((packet->user_agent_line.len >= 21)
	  && (memcmp(packet->user_agent_line.ptr, "YahooMobileMessenger/", 21) == 0)) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO(Mobile)");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	return;
      }

      if (NDPI_SRC_OR_DST_HAS_PROTOCOL(src, dst, NDPI_PROTOCOL_YAHOO)
	  && packet->parsed_lines > 5
	  && memcmp(&packet->payload[5], "/Messenger.", 11) == 0
	  && packet->line[1].len >= 17
	  && ndpi_mem_cmp(packet->line[1].ptr, "Connection: Close",
			  17) == 0 && packet->line[2].len >= 6
	  && ndpi_mem_cmp(packet->line[2].ptr, "Host: ", 6) == 0
	  && packet->line[3].len >= 16
	  && ndpi_mem_cmp(packet->line[3].ptr, "Content-Length: ",
			  16) == 0 && packet->line[4].len >= 23
	  && ndpi_mem_cmp(packet->line[4].ptr, "User-Agent: Mozilla/5.0",
			  23) == 0 && packet->line[5].len >= 23
	  && ndpi_mem_cmp(packet->line[5].ptr, "Cache-Control: no-cache", 23) == 0) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE,
		 "YAHOO HTTP POST P2P FILETRANSFER FOUND\n");
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	return;
      }

      if (packet->host_line.ptr != NULL && packet->host_line.len >= 26 &&
	  ndpi_mem_cmp(packet->host_line.ptr, "filetransfer.msg.yahoo.com", 26) == 0) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE, "YAHOO HTTP POST FILETRANSFER FOUND\n");
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	return;
      }
      /* now check every line */
      for (a = 0; a < packet->parsed_lines; a++) {
	if (packet->line[a].len >= 4 && ndpi_mem_cmp(packet->line[a].ptr, "YMSG", 4) == 0) {
	  NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct,
		   NDPI_LOG_TRACE,
		   "YAHOO HTTP POST FOUND, line is: %.*s\n", packet->line[a].len, packet->line[a].ptr);
	  NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
	  ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	  return;
	}
      }
      if (packet->parsed_lines > 8 && packet->line[8].len > 250 && packet->line[8].ptr != NULL) {
	if (memcmp(packet->line[8].ptr, "<Session ", 9) == 0) {
	  if (ndpi_check_for_YmsgCommand(packet->line[8].len, packet->line[8].ptr)) {
	    NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG,
		     "found HTTP Proxy Yahoo Chat <Ymsg Command= pattern  \n");
	    ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	    return;
	  }
	}
      }
    }
    if (memcmp(packet->payload, "GET /Messenger.", 15) == 0) {
      if ((src != NULL
	   && NDPI_COMPARE_PROTOCOL_TO_BITMASK(src->detected_protocol_bitmask, NDPI_PROTOCOL_YAHOO)
	   != 0) || (dst != NULL
		     && NDPI_COMPARE_PROTOCOL_TO_BITMASK(dst->detected_protocol_bitmask, NDPI_PROTOCOL_YAHOO)
		     != 0)) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE, "YAHOO HTTP GET /Messenger. match\n");
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	return;
      }
    }

    if ((memcmp(packet->payload, "GET /", 5) == 0)) {
      ndpi_parse_packet_line_info(ndpi_struct, flow);
      if ((packet->user_agent_line.ptr != NULL
	   && packet->user_agent_line.len >= NDPI_STATICSTRING_LEN("YahooMobileMessenger/")
	   && memcmp(packet->user_agent_line.ptr, "YahooMobileMessenger/",
		     NDPI_STATICSTRING_LEN("YahooMobileMessenger/")) == 0)
	  || (packet->user_agent_line.len >= 15
	      && (memcmp(packet->user_agent_line.ptr, "Y!%20Messenger/", 15) == 0))) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO(Mobile)");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	return;
      }
      if (packet->host_line.ptr != NULL && packet->host_line.len >= NDPI_STATICSTRING_LEN("msg.yahoo.com") &&
	  memcmp(&packet->host_line.ptr[packet->host_line.len - NDPI_STATICSTRING_LEN("msg.yahoo.com")],
		 "msg.yahoo.com", NDPI_STATICSTRING_LEN("msg.yahoo.com")) == 0) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	return;
      }

    }

  }
  /* found another http login command for yahoo, it is like OSCAR */
  /* detect http connections */

  if (packet->payload_packet_len > 50 && (memcmp(packet->payload, "content-length: ", 16) == 0)) {
    ndpi_parse_packet_line_info(ndpi_struct, flow);
    if (packet->parsed_lines > 2 && packet->line[1].len == 0) {
      NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE, "first line is empty.\n");
      if (packet->line[2].len > 13 && memcmp(packet->line[2].ptr, "<Ymsg Command=", 14) == 0) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE, "YAHOO web chat found\n");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_REAL_PROTOCOL);
	return;
      }
    }
  }

  if (packet->payload_packet_len > 38 && memcmp(packet->payload, "CONNECT scs.msg.yahoo.com:5050 HTTP/1.", 38) == 0) {
    NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE, "YAHOO-HTTP FOUND\n");
    NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
    ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
    return;
  }

  if ((src != NULL && NDPI_COMPARE_PROTOCOL_TO_BITMASK(src->detected_protocol_bitmask, NDPI_PROTOCOL_YAHOO) != 0)
      || (dst != NULL
	  && NDPI_COMPARE_PROTOCOL_TO_BITMASK(dst->detected_protocol_bitmask, NDPI_PROTOCOL_YAHOO) != 0)) {
    if (packet->payload_packet_len == 6 && memcmp(packet->payload, "YAHOO!", 6) == 0) {
      NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
      ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_REAL_PROTOCOL);
      return;
    }
    /* asymmetric detection for SNDIMG not done yet.
     * See ./Yahoo8.1-VideoCall-LAN.pcap and ./Yahoo-VideoCall-inPublicIP.pcap */


    if (packet->payload_packet_len == 8
	&& (memcmp(packet->payload, "<SNDIMG>", 8) == 0 || memcmp(packet->payload, "<REQIMG>", 8) == 0
	    || memcmp(packet->payload, "<RVWCFG>", 8) == 0 || memcmp(packet->payload, "<RUPCFG>", 8) == 0)) {
      NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_TRACE,
	       "YAHOO SNDIMG or REQIMG or RVWCFG or RUPCFG FOUND\n");
      if (src != NULL) {
	if (memcmp(packet->payload, "<SNDIMG>", 8) == 0) {
	  src->yahoo_video_lan_dir = 0;
	} else {
	  src->yahoo_video_lan_dir = 1;
	}
	src->yahoo_video_lan_timer = packet->tick_timestamp;
      }
      if (dst != NULL) {
	if (memcmp(packet->payload, "<SNDIMG>", 8) == 0) {
	  dst->yahoo_video_lan_dir = 0;
	} else {
	  dst->yahoo_video_lan_dir = 1;
	}
	dst->yahoo_video_lan_timer = packet->tick_timestamp;

      }
      NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO subtype VIDEO");
      ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_REAL_PROTOCOL);
      return;
    }
    if (src != NULL && packet->tcp->dest == htons(5100)
	&& ((u_int32_t)
	    (packet->tick_timestamp - src->yahoo_video_lan_timer) < ndpi_struct->yahoo_lan_video_timeout)) {
      if (src->yahoo_video_lan_dir == 1) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_REAL_PROTOCOL);
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "IMG MARKED");
	return;
      }

    }
    if (dst != NULL && packet->tcp->dest == htons(5100)
	&& ((u_int32_t)
	    (packet->tick_timestamp - dst->yahoo_video_lan_timer) < ndpi_struct->yahoo_lan_video_timeout)) {
      if (dst->yahoo_video_lan_dir == 0) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO");
	ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_REAL_PROTOCOL);
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "IMG MARKED");
	return;
      }

    }
  }

  /* detect YAHOO over HTTP proxy */
#ifdef NDPI_PROTOCOL_HTTP
  if (packet->detected_protocol_stack[0] == NDPI_PROTOCOL_HTTP)
#endif
    {

      if (flow->l4.tcp.yahoo_http_proxy_stage == 0) {
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG,
		 "YAHOO maybe HTTP proxy packet 1 => need next packet\n");
	flow->l4.tcp.yahoo_http_proxy_stage = 1 + packet->packet_direction;
	return;
      }
      if (flow->l4.tcp.yahoo_http_proxy_stage == 1 + packet->packet_direction) {
	if ((packet->payload_packet_len > 250) && (memcmp(packet->payload, "<Session ", 9) == 0)) {
	  if (ndpi_check_for_YmsgCommand(packet->payload_packet_len, packet->payload)) {
	    NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG,
		     "found HTTP Proxy Yahoo Chat <Ymsg Command= pattern  \n");
	    ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	    return;
	  }
	}
	NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG,
		 "YAHOO maybe HTTP proxy still initial direction => need next packet\n");
	return;
      }
      if (flow->l4.tcp.yahoo_http_proxy_stage == 2 - packet->packet_direction) {

	ndpi_parse_packet_line_info_unix(ndpi_struct, flow);

	if (packet->parsed_unix_lines >= 9) {

	  if (packet->unix_line[4].ptr != NULL && packet->unix_line[4].len >= 9 &&
	      packet->unix_line[8].ptr != NULL && packet->unix_line[8].len >= 6 &&
	      memcmp(packet->unix_line[4].ptr, "<Session ", 9) == 0 &&
	      memcmp(packet->unix_line[8].ptr, "<Ymsg ", 6) == 0) {

	    NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "found YAHOO over HTTP proxy");
	    ndpi_int_yahoo_add_connection(ndpi_struct, flow, NDPI_CORRELATED_PROTOCOL);
	    return;
	  }
	}
      }
    }
  NDPI_ADD_PROTOCOL_TO_BITMASK(flow->excluded_protocol_bitmask, NDPI_PROTOCOL_YAHOO);
}

	
#if !defined(WIN32)
static inline
#else
__forceinline static
#endif
void ndpi_search_yahoo_udp(struct ndpi_detection_module_struct *ndpi_struct, struct ndpi_flow_struct *flow)
{


	
  struct ndpi_id_struct *src = flow->src;
  if (src == NULL || NDPI_COMPARE_PROTOCOL_TO_BITMASK(src->detected_protocol_bitmask, NDPI_PROTOCOL_YAHOO) == 0) {
    goto excl_yahoo_udp;
  }
 excl_yahoo_udp:

  NDPI_ADD_PROTOCOL_TO_BITMASK(flow->excluded_protocol_bitmask, NDPI_PROTOCOL_YAHOO);
}

void ndpi_search_yahoo(struct ndpi_detection_module_struct *ndpi_struct, struct ndpi_flow_struct *flow)
{
  struct ndpi_packet_struct *packet = &flow->packet;
	


  NDPI_LOG(NDPI_PROTOCOL_YAHOO, ndpi_struct, NDPI_LOG_DEBUG, "search yahoo\n");

  if (packet->payload_packet_len > 0 && flow->yahoo_detection_finished == 0) {
    if (packet->tcp != NULL && packet->tcp_retransmission == 0) {

      if (packet->detected_protocol_stack[0] == NDPI_PROTOCOL_UNKNOWN
#ifdef NDPI_PROTOCOL_HTTP
	  || packet->detected_protocol_stack[0] == NDPI_PROTOCOL_HTTP
#endif
#ifdef NDPI_PROTOCOL_SSL
	  || packet->detected_protocol_stack[0] == NDPI_PROTOCOL_SSL
#endif
	  ) {
	ndpi_search_yahoo_tcp(ndpi_struct, flow);
      }
    } else if (packet->udp != NULL) {
      ndpi_search_yahoo_udp(ndpi_struct, flow);
    }
  }
  if (packet->payload_packet_len > 0 && flow->yahoo_detection_finished == 2) {
    if (packet->tcp != NULL && packet->tcp_retransmission == 0) {
      ndpi_search_yahoo_tcp(ndpi_struct, flow);
    }
  }
}
#endif
