/* vim: ft=c ff=unix fenc=utf-8
 * file: test.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ogg/ogg.h>
#include <vorbis/codec.h>

#define OGG_BFSZ 2048
#define CUM_FLAG_WITHEOS 0x01
#define CUM_FLAG_HHOLE   0x02
#define CUM_FLAG_HHEAD   0x04
#define CUM_FLAG_ISFREE  0x08
#define CUM_FLAG_OGGEXC  0x40 /* libogg internal error, unrecoverable */
#define CUM_FLAG_WARNED  0x80 /* non-critical warning */

#define COUT_FLAG_INITED 0x01
#define COUT_FLAG_BREAK  0x40 /* write fail */

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
	struct {
		int fd;
		uint8_t flags;
		size_t size;
		ogg_stream_state state;
	} o;
};

bool
streamout_write (struct stream_cum *stream, ogg_page *page)
{
	register ssize_t ret;
	ret = write (stream->o.fd, (void*)page->header, page->header_len);
	if (ret != -1)
		stream->o.size += ret;
	if (ret == -1 || ret < page->header_len)
		return false;
	ret = write (stream->o.fd, (void*)page->body, page->body_len);
	if (ret != -1)
		stream->o.size += ret;
	if (ret == -1 || ret < page->body_len)
		return false;
	return true;
}

bool
streamout_init (struct stream_cum *stream)
{
	char fname[14]; // (sizeof (uint32_t) << 1) + strlen (".ogg") + 1
	if ((stream->o.flags & COUT_FLAG_INITED) || !(stream->flags & CUM_FLAG_HHEAD))
		return false;
	snprintf (fname, 14, "%x.ogg", stream->serial);
	/* use serial as file name */
	stream->o.fd = open (fname, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
	if (stream->o.fd != -1)
	{
		ogg_stream_init (&stream->o.state, stream->serial);
		stream->o.flags |= COUT_FLAG_INITED;
		return true;
	}
	return false;
}

void
streamout_end (struct stream_cum *stream)
{
	if (!(stream->o.flags & COUT_FLAG_INITED))
		return;
	/* finalize copy */
	close (stream->o.fd);
	// CUM_FLAG_WITHEOS and CUM_FLAG_HHEAD is required
	if (((stream->flags | CUM_FLAG_WITHEOS | CUM_FLAG_HHEAD) & (CUM_FLAG_WITHEOS | CUM_FLAG_HHEAD | CUM_FLAG_WARNED | CUM_FLAG_ISFREE)) == stream->flags)
	{
		/* finalize write */
	}
	else
	{
		/* stream failed, remove file */
		char fname[14]; // (sizeof (uint32_t) << 1) + strelen (".ogg") + 1
		snprintf (fname, 14, "%x.ogg", stream->serial);
		unlink (fname);
	}
	/* free structs */
	ogg_stream_clear (&stream->o.state);
	printf ("<finalize");
	if (stream->o.flags & COUT_FLAG_INITED)
		printf (" defined");
	else
		printf (" undefined");
	if (stream->o.flags & COUT_FLAG_BREAK)
		printf (" buggy>\n");
	else
		printf (" clean>\n");
}

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
	if (stream->packet >= 3 && (stream->flags & CUM_FLAG_HHEAD))
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
	printf ("<have:");
	if (stream->flags & CUM_FLAG_WITHEOS)
		printf (" eos");
	else
		printf (" noeos");
	if (stream->flags & CUM_FLAG_HHEAD)
		printf (" header");
	else
		printf (" noheader");
	if (stream->flags & CUM_FLAG_HHOLE)
		printf (" holes");
	if (stream->flags & CUM_FLAG_OGGEXC)
		printf (" unrecoverable");
	if (stream->flags & CUM_FLAG_WARNED)
		printf (" warning");
	printf (">\n");
	vorbis_comment_clear (&stream->vcomm);
	vorbis_info_clear (&stream->vinfo);
	ogg_stream_clear (&stream->state);
	streamout_end (stream);
}

struct stream_cum *streamlist_check (struct stream_cum *pstream, const ogg_page *page)
{
	register struct stream_cum *lpstream = NULL;
	register uint32_t serial = ogg_page_serialno (page);
	// first check for serial
	for (lpstream = pstream; lpstream && lpstream->serial != serial; lpstream = lpstream->next);
	// alloc new logic stream or realloc old, if page with BOS flag
	if (ogg_page_bos (page))
	{
		// next check for free node
		for (lpstream = pstream; lpstream && !(lpstream->flags & CUM_FLAG_ISFREE); lpstream = lpstream->next);
		if (!lpstream)
		{
			// alloc new
			lpstream = malloc (sizeof (struct stream_cum));
			if (lpstream)
			{
				if (lpstream)
					lpstream->next = pstream ? pstream->next : NULL;
				if (!stream_init (lpstream, serial))
				{
					free (lpstream);
					lpstream = NULL;
				}
				else
				if (pstream)
					pstream->next = lpstream;
			}
		}
		else
		if (pstream && pstream->flags & CUM_FLAG_ISFREE)
		{
			stream_end (pstream);
			if (!stream_init (pstream, serial))
				lpstream = NULL;
		}
	}
	else
	if (pstream && (pstream->flags & CUM_FLAG_ISFREE || ogg_page_eos (page)))
	{
		/* return stream if EOS defined in this page */
		if (!(lpstream->flags & CUM_FLAG_ISFREE))
			lpstream->flags |= (CUM_FLAG_ISFREE | CUM_FLAG_WITHEOS);
		else
			/* really this may indicate hole in stream */
			lpstream = NULL;
	}
	return lpstream;
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
	// calc errors (prevent infinite loops)
	register int errorc = packets;
	register int ret;
	ogg_packet packet;
	if (packets <= 0)
		return;
	// vorbis header in first thee packets (0x01, 0x03, 0x05)
	while (packets > 0)
	{
		if ((ret = ogg_stream_packetout (&stream->state, &packet)) != 1)
		{
			if (ret == 0)
			{
				// set unrecoverable exception
				stream->flags |= (CUM_FLAG_ISFREE | CUM_FLAG_OGGEXC);
				break;
			}
			else
			{
				stream->flags |= CUM_FLAG_WARNED;
				if (errorc-- > 0)
					continue;
				else
				{
					// mark hole in stream
					stream->flags |= CUM_FLAG_HHOLE;
					break;
				}
			}
		}
		// reset errors counter
		errorc = packets;
		// check for hole in stream
		if (!(stream->flags & CUM_FLAG_HHOLE))
			if (stream->packet != packet.packetno)
				stream->flags |= CUM_FLAG_HHOLE;
		// update packets counters
		stream->packet ++;
		packets --;
		// first 3 packets must be contain vorbis header
		if (stream->packet < 3)
		{
			// check first vorbis
			if (stream->packet == 1 && !vorbis_synthesis_idheader (&packet))
			{
				stream->flags &= ~CUM_FLAG_HHEAD; /* nope? */
				break;
			}
			if (!vorbis_synthesis_headerin (&stream->vinfo, &stream->vcomm, &packet))
			{
				stream->flags |= CUM_FLAG_HHEAD;

			}
			else
			{
				stream->flags &= ~CUM_FLAG_HHEAD;
				break;
			}
		}
		if (!(stream->o.flags & COUT_FLAG_BREAK))
		{
			ogg_page opage;
			if (stream->packet == 1)
			{
				// init streaout, write first packet (vorbis_info)
				if (!streamout_init (stream))
					stream->o.flags |= COUT_FLAG_BREAK;
				ogg_stream_packetin (&stream->o.state, &packet);
			}
			else
			if (stream->packet == 3)
			{
				ogg_packet packet_comm;
				/* rebuild comment packet, write current */
				vorbis_commentheader_out (&stream->vcomm, &packet_comm);
				/* write vorbis info */
				/* if ogg_stream_packetin failed, then ogg_stream_flush return zero immediately */
				ogg_stream_packetin (&stream->o.state, &packet_comm);
				ogg_stream_packetin (&stream->o.state, &packet);
				while (ogg_stream_flush (&stream->o.state, &opage))
				{
					if (!streamout_write (stream, &opage))
					{
						stream->o.flags |= COUT_FLAG_BREAK;
						break;
					}
				}
				ogg_packet_clear (&packet_comm);

			}
			else
			// second packet must be omitted
			if (stream->packet != 2)
			{
				// normal copy
				ogg_stream_packetin (&stream->o.state, &packet);
				while (ogg_stream_pageout (&stream->o.state, &opage))
				{
					if (!streamout_write (stream, &opage))
					{
						stream->o.flags |= COUT_FLAG_BREAK;
						break;
					}
				}
			}
		}
	}
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
				if (!ogg_stream_pagein (&cstream->state, &page))
				{
					if ((ret = ogg_page_packets (&page)) > 0)
					{
						cstream->granulepos = ogg_page_granulepos (&page);
						process_packets (&page, ret, cstream);
					}
				}
				/*else
					// TODO: update error counter
				*/
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

