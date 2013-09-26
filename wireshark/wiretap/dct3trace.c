/* dct3trace.c
 * Routines for reading signalling traces generated by Gammu (www.gammu.org)
 * from Nokia DCT3 phones in Netmonitor mode.
 *
 * gammu --nokiadebug nhm5_587.txt v18-19
 *
 * Duncan Salerno <duncan.salerno@googlemail.com>
 *
 * $Id: dct3trace.c 50104 2013-06-21 02:59:54Z guy $
 *
 * Wiretap Library
 * Copyright (c) 1998 by Gilbert Ramirez <gram@alumni.rice.edu>
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

#include "config.h"
#include "wtap-int.h"
#include "buffer.h"
#include "dct3trace.h"
#include "file_wrappers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


/*
   Example downlink data:

<?xml version="1.0"?>
<dump>
<l1 direction="down" logicalchannel="96" physicalchannel="19" sequence="268116" error="0" timeshift="2992" bsic="22" data="31063F100DD0297A53E1020103C802398E0B2B2B2B2B2B" >
<l2 data="063F100DD0297A53E1020103" rest="C802398E0B2B2B2B2B2B" >
</l2>
</l1>
</dump>

   Example uplink data (no raw L1):

<?xml version="1.0"?>
<dump>
<l1 direction="up" logicalchannel="112" >
<l2 type="U" subtype="Unknown" p="0" data="061500400000000000000000000000000000" >
</l2>
</l1>
</dump>

 */


/* Magic text to check */
static const char dct3trace_magic_line1[] = "<?xml version=\"1.0\"?>";
static const char dct3trace_magic_line2[] = "<dump>";
static const char dct3trace_magic_record_start[]  = "<l1 ";
static const char dct3trace_magic_record_end[]  = "</l1>";
static const char dct3trace_magic_l2_start[]  = "<l2 ";
static const char dct3trace_magic_l2_end[]  = "</l2>";
static const char dct3trace_magic_end[]  = "</dump>";

#define MAX_PACKET_LEN 23

static gboolean dct3trace_read(wtap *wth, int *err, gchar **err_info,
	gint64 *data_offset);
static gboolean dct3trace_seek_read(wtap *wth, gint64 seek_off,
	struct wtap_pkthdr *phdr, Buffer *buf, int len,
	int *err, gchar **err_info);

/*
 * Following 3 functions taken from gsmdecode-0.7bis, with permission - http://wiki.thc.org/gsm
 */

static int
hc2b(unsigned char hex)
{
	hex = tolower(hex);
	if ((hex >= '0') && (hex <= '9'))
		return hex - '0';
	if ((hex >= 'a') && (hex <= 'f'))
		return hex - 'a' + 10;
	return -1;
}

static int
hex2bin(guint8 *out, guint8 *out_end, char *in)
{
	guint8 *out_start = out;
	int is_low = 0;
	int c;

	while (*in != '\0')
	{
		c = hc2b(*(unsigned char *)in);
		if (c < 0)
		{
			in++;
			continue;
		}
		if (out == out_end)
		{
			/* Too much data */
			return -1;
		}
		if (is_low == 0)
		{
			*out = c << 4;
			is_low = 1;
		} else {
			*out |= (c & 0x0f);
			is_low = 0;
			out++;
		}
		in++;
	}

	return (int)(out - out_start);
}

static int
xml_get_int(int *val, const char *str, const char *pattern)
{
	const char *ptr;
	char *start, *end;
	char buf[32];

	ptr = strstr(str, pattern);
	if (ptr == NULL)
		return -1;
	start = strchr(ptr, '"');
	if (start == NULL)
		return -2;
	start++;
	end = strchr(start, '"');
	if (end == NULL)
		return -3;
	if (end - start > 31)
		return -4;

	memcpy(buf, start, end - start);
	buf[end - start] = '\0';
	*val = atoi(buf);
	return 0;
}


int dct3trace_open(wtap *wth, int *err, gchar **err_info)
{
	char line1[64], line2[64];

	/* Look for Gammu DCT3 trace header */
	if (file_gets(line1, sizeof(line1), wth->fh) == NULL ||
		file_gets(line2, sizeof(line2), wth->fh) == NULL)
	{
		*err = file_error(wth->fh, err_info);
		if (*err != 0 && *err != WTAP_ERR_SHORT_READ)
			return -1;
		return 0;
	}

	/* Don't compare line endings */
	if( strncmp(dct3trace_magic_line1, line1, strlen(dct3trace_magic_line1)) != 0 ||
		strncmp(dct3trace_magic_line2, line2, strlen(dct3trace_magic_line2)) != 0)
	{
		return 0;
	}

	wth->file_encap = WTAP_ENCAP_GSM_UM;
	wth->file_type = WTAP_FILE_DCT3TRACE;
	wth->snapshot_length = 0; /* not known */
	wth->subtype_read = dct3trace_read;
	wth->subtype_seek_read = dct3trace_seek_read;
	wth->tsprecision = WTAP_FILE_TSPREC_SEC;

	return 1;
}


static gboolean dct3trace_get_packet(FILE_T fh, struct wtap_pkthdr *phdr,
	Buffer *buf, int *err, gchar **err_info)
{
	char line[1024];
	guint8 databuf[MAX_PACKET_LEN], *bufp;
	gboolean have_data = FALSE;
	int len = 0;

	bufp = &databuf[0];
	while (file_gets(line, sizeof(line), fh) != NULL)
	{
		if( memcmp(dct3trace_magic_end, line, strlen(dct3trace_magic_end)) == 0 )
		{
			/* Return on end of file </dump> */
			*err = 0;
			return FALSE;
		}
		else if( memcmp(dct3trace_magic_record_end, line, strlen(dct3trace_magic_record_end)) == 0 )
		{
			/* Return on end of record </l1> */
			if( have_data )
			{
				/* We've got a full packet! */
				phdr->presence_flags = 0; /* no time stamp, no separate "on the wire" length */
				phdr->ts.secs = 0;
				phdr->ts.nsecs = 0;
				phdr->caplen = len;
				phdr->len = len;

				*err = 0;

				/* Make sure we have enough room for the packet */
				buffer_assure_space(buf, phdr->caplen);
				memcpy( buffer_start_ptr(buf), databuf, phdr->caplen );

				return TRUE;
			}
			else
			{
				/* If not got any data return error */
				*err = WTAP_ERR_BAD_FILE;
				*err_info = g_strdup_printf("dct3trace: record without data");
				return FALSE;
			}
		}
		else if( memcmp(dct3trace_magic_record_start, line, strlen(dct3trace_magic_record_start)) == 0 )
		{
			/* Parse L1 header <l1 ...>*/
			int channel, tmp;
			char *ptr;

			phdr->pseudo_header.gsm_um.uplink = !strstr(line, "direction=\"down\"");
			if (xml_get_int(&channel, line, "logicalchannel") != 0)
				goto baddata;

			/* Parse downlink only fields */
			if( !phdr->pseudo_header.gsm_um.uplink )
			{
				if (xml_get_int(&tmp, line, "physicalchannel") != 0)
					goto baddata;
				phdr->pseudo_header.gsm_um.arfcn = tmp;
				if (xml_get_int(&tmp, line, "sequence") != 0)
					goto baddata;
				phdr->pseudo_header.gsm_um.tdma_frame = tmp;
				if (xml_get_int(&tmp, line, "bsic") != 0)
					goto baddata;
				phdr->pseudo_header.gsm_um.bsic = tmp;
				if (xml_get_int(&tmp, line, "error") != 0)
					goto baddata;
				phdr->pseudo_header.gsm_um.error = tmp;
				if (xml_get_int(&tmp, line, "timeshift") != 0)
					goto baddata;
				phdr->pseudo_header.gsm_um.timeshift = tmp;
			}

			switch( channel )
			{
				case 128: phdr->pseudo_header.gsm_um.channel = GSM_UM_CHANNEL_SDCCH; break;
				case 112: phdr->pseudo_header.gsm_um.channel = GSM_UM_CHANNEL_SACCH; break;
				case 176: phdr->pseudo_header.gsm_um.channel = GSM_UM_CHANNEL_FACCH; break;
				case 96: phdr->pseudo_header.gsm_um.channel = GSM_UM_CHANNEL_CCCH; break;
				case 80: phdr->pseudo_header.gsm_um.channel = GSM_UM_CHANNEL_BCCH; break;
				default: phdr->pseudo_header.gsm_um.channel = GSM_UM_CHANNEL_UNKNOWN; break;
			}

			/* Read data (if have it) into databuf */
			ptr = strstr(line, "data=\"");
			if( ptr )
			{
				have_data = TRUE; /* If has data... */
				len = hex2bin(bufp, &databuf[MAX_PACKET_LEN], ptr+6);
				if (len == -1)
				{
					*err = WTAP_ERR_BAD_FILE;
					*err_info = g_strdup_printf("dct3trace: record length %d too long", phdr->caplen);
					return FALSE;
				}
			}
		}
		else if( !have_data && memcmp(dct3trace_magic_l2_start, line, strlen(dct3trace_magic_l2_start)) == 0 )
		{
			/* For uplink packets we might not get the raw L1, so have to recreate it from the L2 */
			/* Parse L2 header if didn't get data from L1 <l2 ...> */
			int data_len;
			char *ptr = strstr(line, "data=\"");

			if( !ptr )
			{
				continue;
			}

			have_data = TRUE;

			/*
			 * We know we have no data already, so we know
			 * we have enough room for the header.
			 */
			if( phdr->pseudo_header.gsm_um.channel == GSM_UM_CHANNEL_SACCH || phdr->pseudo_header.gsm_um.channel == GSM_UM_CHANNEL_FACCH || phdr->pseudo_header.gsm_um.channel == GSM_UM_CHANNEL_SDCCH )
			{
				/* Add LAPDm B header */
				memset(bufp, 0x1, 2);
				len = 3;
			}
			else
			{
				/* Add LAPDm Bbis header */
				len = 1;
			}
			bufp += len;

			data_len = hex2bin(bufp, &databuf[MAX_PACKET_LEN], ptr+6);
			if (data_len == -1)
			{
				*err = WTAP_ERR_BAD_FILE;
				*err_info = g_strdup_printf("dct3trace: record length %d too long", phdr->caplen);
				return FALSE;
			}
			len += data_len;

			/* Add LAPDm length byte */
			*(bufp - 1) = data_len << 2 | 0x1;
		}
	}

	*err = file_error(fh, err_info);
	if (*err == 0)
	{
		*err = WTAP_ERR_SHORT_READ;
	}
	return FALSE;

baddata:
	*err = WTAP_ERR_BAD_FILE;
	*err_info = g_strdup_printf("dct3trace: record missing mandatory attributes");
	return FALSE;
}


/* Find the next packet and parse it; called from wtap_read(). */
static gboolean dct3trace_read(wtap *wth, int *err, gchar **err_info,
    gint64 *data_offset)
{
	*data_offset = file_tell(wth->fh);

	return dct3trace_get_packet(wth->fh, &wth->phdr, wth->frame_buffer,
	    err, err_info);
}


/* Used to read packets in random-access fashion */
static gboolean dct3trace_seek_read(wtap *wth, gint64 seek_off,
	struct wtap_pkthdr *phdr, Buffer *buf, int len _U_,
	int *err, gchar **err_info)
{
	if (file_seek(wth->random_fh, seek_off, SEEK_SET, err) == -1)
	{
		return FALSE;
	}

	return dct3trace_get_packet(wth->random_fh, phdr, buf, err, err_info);
}
