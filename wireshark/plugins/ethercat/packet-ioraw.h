/* packet-ioraw.h
 *
 * $Id: packet-ioraw.h 46748 2012-12-26 05:57:06Z guy $
 *
 * Copyright (c) 2007 by Beckhoff Automation GmbH
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PACKET_IORAW_H_
#define _PACKET_IORAW_H_

/* headers are only used for size and offset calculation*/
typedef struct _IoRawParser
{
   guint32 head;
} IoRawParserHDR, *PIoRawParserHDR;
#define IoRawParserHDR_Len (int)sizeof(IoRawParserHDR)

#endif /* _PACKET_IORAW_H_*/
