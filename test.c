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

struct stream_cum
{
	ogg_stream_state state;
	ogg_page page;
	vorbis_info vinfo;
	vorbis_comment vcomm;
};

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
							(void*)vcom->user_comments, (void*)vcom->comment_lengths, vcom->comments, vcom->vendor);
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
		printf ("serial=0x%x, bos=%d, eos=%d, gran=%lu, cont=%d, pageno=%ld, packets=%d, "
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

void
process_page (struct stream_cum *stream, ogg_sync_state *state)
{
	// TODO
	return;
}

void
process_fd (int fd)
{
	char *buffer;
	struct stream_cum stream;
	ogg_sync_state state;
	ogg_sync_init (&state);
	int ret;
	vorbis_comment_init (&stream.vcomm);
	vorbis_info_init (&stream.vinfo);
	while (true)
	{
		ret = ogg_sync_pageseek (&state, &stream.page);

		if (ret > 0)
		{
			process_page (&stream, &state);
		}
		else
		if (!ret)
		{
			buffer = ogg_sync_buffer (&state, OGG_BFSZ);
			if (buffer)
			{
				ret = read (fd, buffer, OGG_BFSZ);
				if (ret > 0)
				{
					ogg_sync_wrote (&state, ret);
				}
				else
				{
					// EOF or exception gained
					break;
				}
			}
			else
			{
				// TODO: update error counter
			}
		}
		else
		{
			// TODO: update error counter
		}
	}
	//
	vorbis_comment_clear (&stream.vcomm);
	vorbis_info_clear (&stream.vinfo);
	ogg_sync_clear (&state);
}

int
main (int argc, char *argv[])
{
	int fd;
	if (argc < 2)
	{
		perror ("input file not set");
		return EXIT_FAILURE;
	}
	fd = open (argv[1], O_RDONLY);
	if (fd == -1)
	{
		perror ("open");
		return EXIT_FAILURE;
	}
	process_fd (fd);
	close (fd);
	return EXIT_SUCCESS;
}

