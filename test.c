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
#define CUM_FLAG_WITHEOS 1
#define CUM_FLAG_NOHOLES 2
#define CUM_FLAG_NOHEAD  4
#define CUM_FLAG_ISFREE  8

struct stream_cum
{
	struct stream_cum *next;
	uint32_t serial; /* stream serial code */
	size_t packet; /* last packet number */
	uint8_t flags;
	uint64_t granulepos;
	ogg_stream_state state;
	vorbis_info vinfo;
	vorbis_comment vcomm;
};

bool
stream_init (struct stream_cum *stream, uint32_t serial)
{
	memset (stream, 0, sizeof (struct stream_cum));
	if (!ogg_stream_init (&stream->state, serial))
	{
		vorbis_comment_init (&stream->vcomm);
		vorbis_info_init (&stream->vinfo);
		stream->serial = serial;
		return true;
	}
	return false;
}

void
stream_end (struct stream_cum *stream)
{
	unsigned int i;
	if (stream->packet >= 3 && !(stream->flags & CUM_FLAG_NOHEAD))
	{
		double time;
		printf ("<stream 0x%x, vorbis>\n", stream->serial);
		printf ("<vorbis version=%d, channels=%d, rate=%ld, "
				"bitrate_upper=%ld, bitrate_nominal=%ld, "
				"bitrate_lower=%ld, bitrate_window=%ld, "
				"codec_setup=%p>\n",
				stream->vinfo.version, stream->vinfo.channels, stream->vinfo.rate,
				stream->vinfo.bitrate_upper, stream->vinfo.bitrate_nominal,
				stream->vinfo.bitrate_lower, stream->vinfo.bitrate_window,
				stream->vinfo.codec_setup);
		printf ("<vorbis user_comments=%p, comment_lengths=%p, comments=%d, vendor='%s'>\n",
				(void*)stream->vcomm.user_comments, (void*)stream->vcomm.comment_lengths, stream->vcomm.comments, stream->vcomm.vendor);
		for (i = 0; i < stream->vcomm.comments; i ++)
		{
			printf ("<comment '%s'>\n", stream->vcomm.user_comments[i]);
		}
		time = (double)stream->granulepos / stream->vinfo.rate;
		printf ("<time: %.3fseconds>\n", time);
	}
	else
	{
		printf ("<stream 0x%x, unknown>\n", stream->serial);
	}
	vorbis_comment_clear (&stream->vcomm);
	vorbis_info_clear (&stream->vinfo);
	ogg_stream_clear (&stream->state);
}

struct stream_cum *streamlist_check (struct stream_cum *pstream, const ogg_page *page)
{
	register struct stream_cum *lpstream = NULL;
	register uint32_t serial = ogg_page_serialno (page);
	while (pstream && !(pstream->flags & CUM_FLAG_ISFREE) && pstream->serial != serial && (pstream = (lpstream = pstream)->next));
	// alloc new logic stream or realloc old, if page with BOS flag
	if (ogg_page_bos (page))
	{
		if (!pstream)
		{
			// alloc new
			pstream = malloc (sizeof (struct stream_cum));
			if (pstream)
			{
				if (lpstream)
					lpstream->next = pstream;
				if (!stream_init (pstream, serial))
				{
					free (pstream);
					pstream = NULL;
				}
			}
		}
		else
		if (pstream && pstream->flags & CUM_FLAG_ISFREE)
		{
			stream_end (pstream);
			if (!stream_init (pstream, serial))
				pstream = NULL;
		}
	}
	else
	if (pstream && (pstream->flags & CUM_FLAG_ISFREE || ogg_page_eos (page)))
	{
		if (!(pstream->flags & CUM_FLAG_ISFREE))
			pstream->flags |= CUM_FLAG_ISFREE;
		pstream = NULL;
	}
	return pstream;
}

void
streamlist_free (struct stream_cum *pstream)
{
	register struct stream_cum *lpstream;
	while (pstream)
	{
		pstream = (lpstream = pstream)->next;
		stream_end (lpstream);
		free (lpstream);
	}
}

void
process_packets (ogg_page *page, int packets, struct stream_cum *stream)
{
	ogg_packet packet;
	if (packets <= 0)
		return;
	// vorbis header in first thee packets (0x01, 0x03, 0x05)
	while (stream->packet < 3 && packets > 0)
	{
		if (ogg_stream_packetout (&stream->state, &packet) != 1)
		{
			// TODO: update exception counter
			continue;
		}
		// update packets counters
		packets --;
		stream->packet ++;
		// check first vorbis
		if (stream->packet == 1 && !vorbis_synthesis_idheader (&packet))
		{
			stream->flags |= (CUM_FLAG_NOHEAD | CUM_FLAG_ISFREE);
			break;
		}
		if (!vorbis_synthesis_headerin (&stream->vinfo, &stream->vcomm, &packet))
			stream->flags &= ~CUM_FLAG_NOHEAD; /* nope? */
		else
		{
			stream->flags |= (CUM_FLAG_NOHEAD | CUM_FLAG_ISFREE);
			break;
		}
	}
	if (packets > 0)
		stream->packet += packets;
	return;
}

void
process_fd (int fd)
{
	char *buffer;
	struct stream_cum *stream = NULL;
	struct stream_cum *cstream;
	ogg_page page;
	ogg_sync_state state;
	ogg_sync_init (&state);
	register int ret;
	while (true)
	{
		ret = ogg_sync_pageseek (&state, &page);

		if (ret > 0)
		{
			cstream = streamlist_check (stream, &page);
			if (cstream)
			{
				if (!stream)
					stream = cstream;
				if ((ret = ogg_page_packets (&page)) > 0)
				{
					if (!ogg_stream_pagein (&cstream->state, &page))
					{
						cstream->granulepos = ogg_page_granulepos (&page);
						process_packets (&page, ret, cstream);
					}
					/*else
						// TODO: update error counter
					*/
				}
			}
			else
			{
				// TODO: update error counter
			}
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
	streamlist_free (stream);
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

