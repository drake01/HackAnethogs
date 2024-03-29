/* 
 * nethogs.cpp
 *
 * Copyright (c) 2004-2006,2008,2011 Arnout Engelen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "nethogs.h"

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <string>
#include <string.h>
#include <getopt.h>
#include <stdarg.h>

#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include "cui.h"

extern "C" {
	#include "decpcap.h"
}

#include "packet.h"
#include "connection.h"
#include "process.h"
#include "refresh.h"
#include "devices.h"

extern Process * unknownudp;

unsigned refreshdelay = 1;
bool tracemode = false;
bool bughuntmode = false;
bool needrefresh = true;
//packet_type packettype = packet_ethernet;
//dp_link_type linktype = dp_link_ethernet;
const char version[] = " version " VERSION "." SUBVERSION "." MINORVERSION;

const char * currentdevice = NULL;

timeval curtime;

bool local_addr::contains (const in_addr_t & n_addr) {
	if ((sa_family == AF_INET)
	    && (n_addr == addr))
		return true;
	if (next == NULL)
		return false;
	return next->contains(n_addr);
}

bool local_addr::contains(const struct in6_addr & n_addr) {
	if (sa_family == AF_INET6)
	{
		/*
		if (DEBUG) {
			char addy [50];
			std::cerr << "Comparing: ";
			inet_ntop (AF_INET6, &n_addr, addy, 49);
			std::cerr << addy << " and ";
			inet_ntop (AF_INET6, &addr6, addy, 49);
			std::cerr << addy << std::endl;
		}
		*/
		//if (addr6.s6_addr == n_addr.s6_addr)
		if (memcmp (&addr6, &n_addr, sizeof(struct in6_addr)) == 0)
		{
			if (DEBUG)
				std::cerr << "Match!" << std::endl;
			return true;
		}
	}
	if (next == NULL)
		return false;
	return next->contains(n_addr);
}

struct dpargs {
	int sa_family;
	in_addr ip_src;
	in_addr ip_dst;
	in6_addr ip6_src;
	in6_addr ip6_dst;
};

const char* getVersion()
{
	return version;
}

int process_tcp (u_char * userdata, const dp_header * header, const u_char * m_packet) {
	struct dpargs * args = (struct dpargs *) userdata;
	struct tcphdr * tcp = (struct tcphdr *) m_packet;

	curtime = header->ts;

	/* get info from userdata, then call getPacket */
	Packet * packet;
	switch (args->sa_family)
	{
		case (AF_INET):
			packet = new Packet (args->ip_src, ntohs(tcp->source), args->ip_dst, ntohs(tcp->dest), header->len, header->ts);
			break;
		case (AF_INET6):
			packet = new Packet (args->ip6_src, ntohs(tcp->source), args->ip6_dst, ntohs(tcp->dest), header->len, header->ts);
			break;
	}

	Connection * connection = findConnection(packet);

	if (connection != NULL)
	{
		/* add packet to the connection */
		connection->add(packet);
	} else {
		/* else: unknown connection, create new */
		connection = new Connection (packet);
		getProcess(connection, currentdevice);
	}
	delete packet;

	if (needrefresh)
	{
		do_refresh();
		needrefresh = false;
	}

	/* we're done now. */
	return true;
}

int process_udp (u_char * userdata, const dp_header * header, const u_char * m_packet) {
	struct dpargs * args = (struct dpargs *) userdata;
	//struct tcphdr * tcp = (struct tcphdr *) m_packet;
	struct udphdr * udp = (struct udphdr *) m_packet;

	curtime = header->ts;

	/* TODO get info from userdata, then call getPacket */
	Packet * packet;
	switch (args->sa_family)
	{
		case (AF_INET):
			packet = new Packet (args->ip_src, ntohs(udp->source), args->ip_dst, ntohs(udp->dest), header->len, header->ts);
			break;
		case (AF_INET6):
			packet = new Packet (args->ip6_src, ntohs(udp->source), args->ip6_dst, ntohs(udp->dest), header->len, header->ts);
			break;
	}

	//if (DEBUG)
	//	std::cout << "Got packet from " << packet->gethashstring() << std::endl;

	Connection * connection = findConnection(packet);

	if (connection != NULL)
	{
		/* add packet to the connection */
		connection->add(packet);
	} else {
		/* else: unknown connection, create new */
		connection = new Connection (packet);
		getProcess(connection, currentdevice);
	}
	delete packet;

	if (needrefresh)
	{
		do_refresh();
		needrefresh = false;
	}

	/* we're done now. */
	return true;
}

int process_ip (u_char * userdata, const dp_header * /* header */, const u_char * m_packet) {
	struct dpargs * args = (struct dpargs *) userdata;
	struct ip * ip = (struct ip *) m_packet;
	args->sa_family = AF_INET;
	args->ip_src = ip->ip_src;
	args->ip_dst = ip->ip_dst;

	/* we're not done yet - also parse tcp :) */
	return false;
}

int process_ip6 (u_char * userdata, const dp_header * /* header */, const u_char * m_packet) {
	struct dpargs * args = (struct dpargs *) userdata;
	const struct ip6_hdr * ip6 = (struct ip6_hdr *) m_packet;
	args->sa_family = AF_INET6;
	args->ip6_src = ip6->ip6_src;
	args->ip6_dst = ip6->ip6_dst;

	/* we're not done yet - also parse tcp :) */
	return false;
}

void quit_cb (int /* i */)
{
	procclean();
	if ((!tracemode) && (!DEBUG))
		exit_ui();
	exit(0);
}

void forceExit(bool success, const char *msg, ...)
{
	if ((!tracemode)&&(!DEBUG)){
		exit_ui();
	}

	va_list argp;
	va_start(argp, msg);
	vfprintf(stderr, msg, argp);
	va_end(argp);
	std::cerr << std::endl;

	if (success)
		exit(EXIT_SUCCESS);
	else
		exit(EXIT_FAILURE);
}

class handle {
public:
	handle (dp_handle * m_handle, const char * m_devicename = NULL,
			handle * m_next = NULL) {
		content = m_handle; next = m_next; devicename = m_devicename;
	}
	dp_handle * content;
	const char * devicename;
	handle * next;
};

