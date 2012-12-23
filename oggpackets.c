/* vim: ft=c ff=unix fenc=utf-8
 * file: test.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ogg/ogg.h>
#include <vorbis/codec.h>

#define OGG_BFSZ 2048

void
print_vorbis (ogg_packet *packet, vorbis_info *vinfo, vorbis_comment *vcom)
{
	register size_t i;
	switch (vorbis_synthesis_headerin (vinfo, vcom, packet))
	{
		case OV_ENOTVORBIS:
			printf ("<vorbis not vorbis>\n");
			break;
		case OV_EBADHEADER:
			printf ("<vorbis bad header>\n");
			break;
		case OV_EFAULT:
			printf ("<vorbis internal error>\n");
			break;
		case 0:
			switch (packet->packetno)
			{
				case 0:
					printf ("<vorbis version=%d, channels=%d, rate=%ld, "
							"bitrate_upper=%ld, bitrate_nominal=%ld, "
							"bitrate_lower=%ld, bitrate_window=%ld, "
							"codec_setup=%p>\n",
							vinfo->version, vinfo->channels, vinfo->rate,
							vinfo->bitrate_upper, vinfo->bitrate_nominal,
							vinfo->bitrate_lower, vinfo->bitrate_window,
							vinfo->codec_setup);
					break;
				case 1:
					printf ("<vorbis user_comments=%p, comment_lengths=%p, comments=%d, vendor='%s'>\n",
							vcom->user_comments, vcom->comment_lengths, vcom->comments, vcom->vendor);
					for (i = 0; i < vcom->comments; i ++)
					{
						printf ("<comment '%s'>\n", vcom->user_comments[i]);
					}
					break;
			}
			break;
	}
}

void
print_ogpack (ogg_packet *packet, vorbis_info *vinfo, vorbis_comment *vcom)
{
	printf ("<ogg_packet packet=%p, bytes=%ld, b_o_s=%ld, e_o_s=%ld, granulepos=%ld, packetno=%ld, "
			"0x%02x%02x%02x%02x>\n",
			packet->packet, packet->bytes, packet->b_o_s, packet->e_o_s,
			packet->granulepos, packet->packetno,
			packet->packet[0], packet->packet[1], packet->packet[2], packet->packet[3]);
	print_vorbis (packet, vinfo, vcom);
}

void
print_ogp (ogg_page *ogp, bool nog, ogg_stream_state *state, vorbis_info *vinfo, vorbis_comment *vcom)
{
	/*
		typedef struct {
			unsigned char *header;
			long           header_len;
			unsigned char *body;
			long           body_len;
		} ogg_page;
	 */
	printf ("<ogg_page header=%p, header_len=%ld, body=%p, body_len=%ld, ",
			(void*)ogp->header, ogp->header_len,
			(void*)ogp->body, ogp->body_len);
	if (ogp->body)
	{
		printf ("serial=0x%x, bos=%d, eos=%d, gran=%ld, cont=%d, pageno=%ld, packets=%d, "
				"0x%02x%02x%02x%02x>\n",
			ogg_page_serialno (ogp),
			ogg_page_bos (ogp),
			ogg_page_eos (ogp),
			ogg_page_granulepos (ogp),
			ogg_page_continued (ogp),
			ogg_page_pageno (ogp),
			ogg_page_packets (ogp),
			ogp->body[0], ogp->body[1], ogp->body[2], ogp->body[3]
			);
		if ((ogg_page_pageno (ogp) == 0 && !ogg_stream_init (state, ogg_page_serialno (ogp))) || true)
		{
			if (ogg_page_pageno (ogp) < 3 && !nog)
			{
				ogg_packet packet;
				//printf ("HE\n");
				//print_ogp (ogp, true);
				ogg_stream_pagein (state, ogp);
				if (ogg_stream_packetout (state, &packet) == 1)
					print_ogpack (&packet, vinfo, vcom);
				if (ogg_stream_packetout (state, &packet) == 1)
					print_ogpack (&packet, vinfo, vcom);
			}
		}
	}
	else
	{
		printf ("null>\n");
	}
}

void
print_ogss (ogg_sync_state *ogss)
{
	/*
	typedef struct {
		unsigned char *data;
		int storage;
		int fill;
		int returned;

		int unsynced;
		int headerbytes;
		int bodybytes;
	} ogg_sync_state;
	*/
	printf ("<ogg_sync_state data=%p, storage=%d, fill=%d, returned=%d, unsynced=%d, headerbytes=%d, bodybytes=%d>\n",
			ogss->data, ogss->storage, ogss->fill, ogss->returned,
			ogss->unsynced, ogss->headerbytes, ogss->bodybytes);
}

int
main (int argc, char *argv[])
{
	vorbis_info vinfo;
	vorbis_comment vcom;
	ogg_stream_state og_sstate;
	ogg_sync_state og_sync;
	ogg_page og_page;
	char *buffer;
	int fio;
	int r;
	int wrc = 0;
	if (argc < 2)
	{
		perror ("input file not set");
		return -1;
	}
	fio = open (argv[1], O_RDONLY);
	if (fio == -1) {
		perror ("can't open input file");
		return EXIT_FAILURE;
	}
	ogg_sync_init (&og_sync);
	vorbis_info_init (&vinfo);
	vorbis_comment_init (&vcom);
	while (true)
	{
		r = ogg_sync_pageseek (&og_sync, &og_page);
		if (r < 0)
			printf ("hole in %d bytes\n", -r);
		else
		if (r > 0)
		{
			print_ogp (&og_page, false, &og_sstate, &vinfo, &vcom);
		}
		else
		{
			buffer = ogg_sync_buffer (&og_sync, OGG_BFSZ);
			r = read (fio, buffer, OGG_BFSZ);
			if (r > 0)
			{
				ogg_sync_wrote (&og_sync, r);
				wrc += r;
			}
			else
			{
				// end of stream or stream error
				// (read () returns exception), exit
				printf ("@@ EOF %d\n", r);
				// XXX: wtf?
				//ogg_sync_wrote (&og_sync, 0);
				break;
			}
		}
	}
	ogg_sync_clear (&og_sync);
	ogg_stream_clear (&og_sstate);
	vorbis_info_clear (&vinfo);
	vorbis_comment_clear (&vcom);
	close (fio);
	printf ("wrc: %d\n", wrc);
	return EXIT_SUCCESS;
}

