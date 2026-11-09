/**
 * ctrmus - 3DS Music Player
 * Metadata extraction implementation.
 *
 * Reads title/artist/album tags from MP3 (ID3v2, ID3v1 fallback),
 * Ogg Vorbis / Opus / FLAC-in-Ogg and native FLAC (Vorbis comments) and
 * M4A (iTunes ilst atoms).
 *
 * Everything is parsed by hand on top of the buffered bufio reader rather
 * than through the codec libraries: opening a file with tremor/opusfile
 * also scans the end of the stream to find its length, and dr_flac sets up
 * a full decoder, which is far more SD I/O than reading a few tags needs.
 * Large fields we don't care about (cover art) are skipped with a seek
 * instead of being read. Not using the codec libraries also keeps this
 * code safe to run on the metadata loader thread while the playback thread
 * is decoding. It only uses stack/heap state, so it is reentrant.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "bufio.h"
#include "file.h"
#include "metadata.h"

/* Reads of tag headers are small; cover art is skipped, not read. */
#define META_BUF_SIZE		(16 * 1024)

/* Longest single text field (ID3 frame / Vorbis comment) we bother to
 * read. Anything bigger is not a title/artist/album and gets skipped. */
#define META_MAX_FIELD		2048

enum meta_field
{
	FIELD_NONE = -1,
	FIELD_TITLE,
	FIELD_ARTIST,
	FIELD_ALBUM
};

static uint32_t be32(const unsigned char* b)
{
	return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
	       ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

static uint32_t be24(const unsigned char* b)
{
	return ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
}

static uint32_t le32(const unsigned char* b)
{
	return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) |
	       ((uint32_t)b[1] << 8)  | (uint32_t)b[0];
}

static uint32_t syncsafe(const unsigned char* b)
{
	return ((uint32_t)(b[0] & 0x7F) << 21) | ((uint32_t)(b[1] & 0x7F) << 14) |
	       ((uint32_t)(b[2] & 0x7F) << 7)  | (uint32_t)(b[3] & 0x7F);
}

static bool metaComplete(const struct music_metadata* meta)
{
	return meta->has_title && meta->has_artist && meta->has_album;
}

static bool metaAny(const struct music_metadata* meta)
{
	return meta->has_title || meta->has_artist || meta->has_album;
}

/**
 * Store a field value if it is non-empty and not already set (the first
 * occurrence of a tag wins). Trailing spaces are trimmed.
 */
static void setField(struct music_metadata* meta, enum meta_field f,
                     const char* val, size_t len)
{
	char*	dst;
	bool*	has;
	size_t	cap;

	switch(f)
	{
		case FIELD_TITLE:
			dst = meta->title; has = &meta->has_title;
			cap = sizeof(meta->title);
			break;
		case FIELD_ARTIST:
			dst = meta->artist; has = &meta->has_artist;
			cap = sizeof(meta->artist);
			break;
		case FIELD_ALBUM:
			dst = meta->album; has = &meta->has_album;
			cap = sizeof(meta->album);
			break;
		default:
			return;
	}

	if(*has)
		return;

	/* Stop at an embedded terminator (multi-value ID3v2.4 frames). */
	{
		const char* nul = memchr(val, '\0', len);
		if(nul != NULL)
			len = (size_t)(nul - val);
	}

	while(len > 0 && (val[len - 1] == ' ' || val[len - 1] == '\r' ||
	                  val[len - 1] == '\n'))
		len--;

	if(len == 0)
		return;

	if(len >= cap)
		len = cap - 1;

	memcpy(dst, val, len);
	dst[len] = '\0';
	*has = true;
}

/* ------------------------------------------------------------------ */
/* MP3: ID3v2 and ID3v1                                               */
/* ------------------------------------------------------------------ */

/**
 * Decode an ID3v2 text frame body (encoding byte + text) into a field.
 * ISO-8859-1 and UTF-8 are passed through; UTF-16 is reduced to ASCII.
 */
static void id3TextField(struct music_metadata* meta, enum meta_field f,
                         const unsigned char* data, size_t size)
{
	char	out[sizeof(meta->title)];
	size_t	di = 0;

	if(size < 2)
		return;

	if(data[0] == 1 || data[0] == 2)
	{
		/* UTF-16 with BOM (1) or UTF-16BE without BOM (2). */
		const unsigned char* src = data + 1;
		size_t srcLen = size - 1;
		bool le = false;

		if(srcLen >= 2 && src[0] == 0xFF && src[1] == 0xFE)
		{
			le = true;
			src += 2;
			srcLen -= 2;
		}
		else if(srcLen >= 2 && src[0] == 0xFE && src[1] == 0xFF)
		{
			src += 2;
			srcLen -= 2;
		}
		else if(data[0] == 1)
			le = true;	/* BOM missing: assume LE, the common case. */

		for(size_t i = 0; i + 1 < srcLen && di < sizeof(out) - 1; i += 2)
		{
			uint16_t ch = le ? (uint16_t)(src[i] | (src[i + 1] << 8)) :
			                   (uint16_t)((src[i] << 8) | src[i + 1]);
			if(ch == 0)
				break;
			out[di++] = ch < 128 ? (char)ch : '?';
		}

		setField(meta, f, out, di);
		return;
	}

	if(data[0] == 0 || data[0] == 3)
		setField(meta, f, (const char*)data + 1, size - 1);
}

/**
 * Undo ID3v2 unsynchronisation (0xFF 0x00 -> 0xFF) in place.
 * \return	New length.
 */
static size_t id3Deunsync(unsigned char* d, size_t len)
{
	size_t o = 0;

	for(size_t i = 0; i < len; i++)
	{
		d[o++] = d[i];
		if(d[i] == 0xFF && i + 1 < len && d[i + 1] == 0x00)
			i++;
	}

	return o;
}

static enum meta_field id3FrameField(const unsigned char* id, int version)
{
	if(version == 2)
	{
		if(memcmp(id, "TT2", 3) == 0) return FIELD_TITLE;
		if(memcmp(id, "TP1", 3) == 0) return FIELD_ARTIST;
		if(memcmp(id, "TAL", 3) == 0) return FIELD_ALBUM;
	}
	else
	{
		if(memcmp(id, "TIT2", 4) == 0) return FIELD_TITLE;
		if(memcmp(id, "TPE1", 4) == 0) return FIELD_ARTIST;
		if(memcmp(id, "TALB", 4) == 0) return FIELD_ALBUM;
	}

	return FIELD_NONE;
}

/**
 * Parse an ID3v2 tag starting at the reader's current position.
 * Leaves the reader positioned just after the tag on success.
 *
 * \return	0 if a tag header was found (whether or not it had any fields
 *			we want), -1 if there is no ID3v2 tag here.
 */
static int parseId3v2(struct bufio* b, struct music_metadata* meta)
{
	unsigned char	h[10];
	unsigned char	data[META_MAX_FIELD];
	int64_t			tagStart = bufioTell(b);
	int64_t			tagEnd;
	int				version;
	bool			tagUnsync;
	size_t			frameHdrLen;

	if(bufioRead(b, h, 10) != 10 || memcmp(h, "ID3", 3) != 0)
		return -1;

	version = h[3];
	tagUnsync = (h[5] & 0x80) != 0;
	tagEnd = tagStart + 10 + (int64_t)syncsafe(h + 6);
	if(h[5] & 0x10)
		tagEnd += 10;	/* v2.4 footer */

	if(version < 2 || version > 4)
		goto done;

	/* v2.2 flag 0x40 is "compression" (no defined scheme): give up. */
	if(version == 2 && (h[5] & 0x40))
		goto done;

	/* Skip the extended header. */
	if(version >= 3 && (h[5] & 0x40))
	{
		unsigned char e[4];
		uint32_t esz;

		if(bufioRead(b, e, 4) != 4)
			goto done;

		/* v2.3: size excludes itself; v2.4: syncsafe and includes itself */
		esz = (version == 3) ? be32(e) : syncsafe(e) - 4;
		bufioSeek(b, (int64_t)esz, SEEK_CUR);
	}

	frameHdrLen = (version == 2) ? 6 : 10;

	while(!metaComplete(meta) &&
	      bufioTell(b) + (int64_t)frameHdrLen <= tagEnd)
	{
		unsigned char	fh[10];
		uint32_t		sz;
		unsigned		fflags = 0;
		enum meta_field	f;
		size_t			off = 0;
		size_t			len;
		bool			unsync = tagUnsync;

		if(bufioRead(b, fh, frameHdrLen) != frameHdrLen)
			break;

		/* Padding reached. */
		if(fh[0] == 0)
			break;

		if(version == 2)
			sz = be24(fh + 3);
		else
		{
			sz = (version == 4) ? syncsafe(fh + 4) : be32(fh + 4);
			fflags = fh[9];
		}

		if(bufioTell(b) + (int64_t)sz > tagEnd)
			break;

		f = id3FrameField(fh, version);

		if(f == FIELD_NONE || sz < 2 || sz > sizeof(data))
		{
			/* Not wanted (e.g. APIC cover art): skip without reading. */
			bufioSeek(b, (int64_t)sz, SEEK_CUR);
			continue;
		}

		if(bufioRead(b, data, sz) != sz)
			break;

		if(version == 3)
		{
			if(fflags & 0xC0)		/* compressed / encrypted */
				continue;
			if(fflags & 0x20)		/* grouping identity byte */
				off += 1;
		}
		else if(version == 4)
		{
			if(fflags & 0x0C)		/* compressed / encrypted */
				continue;
			if(fflags & 0x40)		/* grouping identity byte */
				off += 1;
			if(fflags & 0x01)		/* data length indicator */
				off += 4;
			if(fflags & 0x02)
				unsync = true;
		}

		if(off >= sz)
			continue;

		len = sz - off;
		if(unsync)
			len = id3Deunsync(data + off, len);

		id3TextField(meta, f, data + off, len);
	}

done:
	bufioSeek(b, tagEnd, SEEK_SET);
	return 0;
}

/**
 * Fill any still-missing fields from an ID3v1 tag at the end of the file.
 */
static void parseId3v1(struct bufio* b, struct music_metadata* meta)
{
	unsigned char t[128];

	if(bufioSize(b) < 128)
		return;

	if(bufioSeek(b, -128, SEEK_END) != 0 ||
	   bufioRead(b, t, sizeof(t)) != sizeof(t) || memcmp(t, "TAG", 3) != 0)
		return;

	setField(meta, FIELD_TITLE,  (const char*)t + 3,  30);
	setField(meta, FIELD_ARTIST, (const char*)t + 33, 30);
	setField(meta, FIELD_ALBUM,  (const char*)t + 63, 30);
}

/* ------------------------------------------------------------------ */
/* Vorbis comments (shared by Ogg Vorbis, Opus and FLAC)              */
/* ------------------------------------------------------------------ */

/**
 * Byte source for the comment parser. read() with dst == NULL skips.
 * Returns the number of bytes consumed.
 */
struct byteSource
{
	size_t	(*read)(void* ctx, void* dst, size_t n);
	void*	ctx;
};

static int strncasecmp_local(const char* a, const char* b, size_t n)
{
	for(size_t i = 0; i < n; i++)
	{
		char ca = a[i], cb = b[i];
		if(ca >= 'A' && ca <= 'Z') ca += 32;
		if(cb >= 'A' && cb <= 'Z') cb += 32;
		if(ca != cb) return (unsigned char)ca - (unsigned char)cb;
		if(ca == '\0') return 0;
	}
	return 0;
}

static void vorbisCommentField(struct music_metadata* meta,
                               const char* c, size_t len)
{
	static const struct { const char* key; size_t keyLen;
	                      enum meta_field f; } keys[] =
	{
		{ "TITLE=",  6, FIELD_TITLE },
		{ "ARTIST=", 7, FIELD_ARTIST },
		{ "ALBUM=",  6, FIELD_ALBUM },
	};

	for(size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
	{
		if(len > keys[k].keyLen &&
		   strncasecmp_local(c, keys[k].key, keys[k].keyLen) == 0)
		{
			setField(meta, keys[k].f, c + keys[k].keyLen,
			         len - keys[k].keyLen);
			return;
		}
	}
}

/**
 * Parse a Vorbis comment block (vendor string, count, length-prefixed
 * "KEY=value" entries; all lengths little-endian). Entries too large to be
 * a title (e.g. METADATA_BLOCK_PICTURE) are skipped without being read.
 */
static void parseVorbisComments(const struct byteSource* src,
                                struct music_metadata* meta)
{
	unsigned char	n4[4];
	char			buf[META_MAX_FIELD];
	uint32_t		count;

	/* Vendor string */
	if(src->read(src->ctx, n4, 4) != 4)
		return;
	{
		uint32_t vendorLen = le32(n4);
		if(src->read(src->ctx, NULL, vendorLen) != vendorLen)
			return;
	}

	if(src->read(src->ctx, n4, 4) != 4)
		return;
	count = le32(n4);

	for(uint32_t i = 0; i < count && !metaComplete(meta); i++)
	{
		uint32_t len;

		if(src->read(src->ctx, n4, 4) != 4)
			return;
		len = le32(n4);

		if(len > sizeof(buf))
		{
			if(src->read(src->ctx, NULL, len) != len)
				return;
			continue;
		}

		if(src->read(src->ctx, buf, len) != len)
			return;

		vorbisCommentField(meta, buf, len);
	}
}

/* ------------------------------------------------------------------ */
/* Native FLAC                                                        */
/* ------------------------------------------------------------------ */

static size_t bufioSourceRead(void* ctx, void* dst, size_t n)
{
	struct bufio* b = ctx;

	if(dst != NULL)
		return bufioRead(b, dst, n);

	if(bufioTell(b) + (int64_t)n > bufioSize(b))
		return 0;

	bufioSeek(b, (int64_t)n, SEEK_CUR);
	return n;
}

/**
 * Walk FLAC metadata blocks from the "fLaC" marker at the current
 * position, parsing the VORBIS_COMMENT block and seeking over the rest.
 */
static void parseFlac(struct bufio* b, struct music_metadata* meta)
{
	unsigned char			hdr[4];
	const struct byteSource	src = { bufioSourceRead, b };

	if(bufioRead(b, hdr, 4) != 4 || memcmp(hdr, "fLaC", 4) != 0)
		return;

	for(int blocks = 0; blocks < 128; blocks++)
	{
		bool		last;
		unsigned	type;
		uint32_t	len;
		int64_t		next;

		if(bufioRead(b, hdr, 4) != 4)
			return;

		last = (hdr[0] & 0x80) != 0;
		type = hdr[0] & 0x7F;
		len = be24(hdr + 1);
		next = bufioTell(b) + (int64_t)len;

		if(type == 4)	/* VORBIS_COMMENT */
		{
			parseVorbisComments(&src, meta);
			return;
		}

		if(last || type == 127)
			return;

		bufioSeek(b, next, SEEK_SET);
	}
}

/* ------------------------------------------------------------------ */
/* Ogg (Vorbis, Opus, FLAC-in-Ogg)                                    */
/* ------------------------------------------------------------------ */

/**
 * Minimal Ogg packet reader: follows a single logical stream from the
 * start of the file, reading (or skipping) packet bytes across pages.
 */
struct oggReader
{
	struct bufio*	b;
	uint32_t		serial;
	bool			haveSerial;
	unsigned char	lace[255];
	int				nlace;
	int				li;			/* Next lacing value to use. */
	unsigned		curLace;	/* Length of the current segment. */
	uint32_t		segLeft;	/* Unread bytes in the current segment. */
	bool			packetDone;	/* Current packet has ended. */
	bool			eof;
};

static bool oggNextPage(struct oggReader* o)
{
	for(int tries = 0; tries < 64; tries++)
	{
		unsigned char	h[27];
		uint32_t		serial;
		int				nseg;

		if(bufioRead(o->b, h, 27) != 27 || memcmp(h, "OggS", 4) != 0)
			return false;

		serial = le32(h + 14);
		nseg = h[26];

		if(bufioRead(o->b, o->lace, (size_t)nseg) != (size_t)nseg)
			return false;

		/* Page from another multiplexed stream: skip its body. */
		if(o->haveSerial && serial != o->serial)
		{
			int64_t body = 0;
			for(int i = 0; i < nseg; i++)
				body += o->lace[i];
			bufioSeek(o->b, body, SEEK_CUR);
			continue;
		}

		o->serial = serial;
		o->haveSerial = true;
		o->nlace = nseg;
		o->li = 0;
		return true;
	}

	return false;
}

/** byteSource read callback: reads/skips within the current packet. */
static size_t oggPacketRead(void* ctx, void* dst, size_t n)
{
	struct oggReader*	o = ctx;
	unsigned char*		out = dst;
	size_t				done = 0;

	while(done < n && !o->packetDone && !o->eof)
	{
		size_t c;

		if(o->segLeft == 0)
		{
			if(o->li >= o->nlace)
			{
				if(!oggNextPage(o))
					o->eof = true;
				continue;
			}

			o->curLace = o->lace[o->li++];
			o->segLeft = o->curLace;

			if(o->curLace == 0)
				o->packetDone = true;
			continue;
		}

		c = o->segLeft;
		if(c > n - done)
			c = n - done;

		if(out != NULL)
		{
			if(bufioRead(o->b, out + done, c) != c)
			{
				o->eof = true;
				break;
			}
		}
		else
			bufioSeek(o->b, (int64_t)c, SEEK_CUR);

		done += c;
		o->segLeft -= (uint32_t)c;

		/* A segment shorter than 255 bytes terminates the packet. */
		if(o->segLeft == 0 && o->curLace < 255)
			o->packetDone = true;
	}

	return done;
}

/** Skip the rest of the current packet and start the next one. */
static bool oggNextPacket(struct oggReader* o)
{
	while(!o->packetDone && !o->eof)
		oggPacketRead(o, NULL, 1 << 20);

	o->packetDone = false;
	return !o->eof;
}

static void parseOgg(struct bufio* b, struct music_metadata* meta)
{
	struct oggReader		o;
	const struct byteSource	src = { oggPacketRead, &o };
	unsigned char			id[8];

	memset(&o, 0, sizeof(o));
	o.b = b;

	if(!oggNextPage(&o))
		return;

	/* The first packet identifies the codec. */
	if(oggPacketRead(&o, id, sizeof(id)) < 5)
		return;

	if(memcmp(id, "\x01vorbis", 7) == 0)
	{
		unsigned char magic[7];
		if(oggNextPacket(&o) &&
		   oggPacketRead(&o, magic, 7) == 7 &&
		   memcmp(magic, "\x03vorbis", 7) == 0)
			parseVorbisComments(&src, meta);
	}
	else if(memcmp(id, "OpusHead", 8) == 0)
	{
		unsigned char magic[8];
		if(oggNextPacket(&o) &&
		   oggPacketRead(&o, magic, 8) == 8 &&
		   memcmp(magic, "OpusTags", 8) == 0)
			parseVorbisComments(&src, meta);
	}
	else if(memcmp(id, "\x7F" "FLAC", 5) == 0)
	{
		/* Each following header packet is one FLAC metadata block. */
		for(int i = 0; i < 16 && oggNextPacket(&o); i++)
		{
			unsigned char hdr[4];

			if(oggPacketRead(&o, hdr, 4) != 4)
				return;

			if((hdr[0] & 0x7F) == 4)
			{
				parseVorbisComments(&src, meta);
				return;
			}

			if(hdr[0] & 0x80)
				return;
		}
	}
}

/* ------------------------------------------------------------------ */
/* M4A (iTunes-style moov/udta/meta/ilst atoms)                       */
/* ------------------------------------------------------------------ */

/**
 * Find a named atom within [start, end).
 * Returns the offset of the atom body and sets *bodySize, or -1.
 */
static int64_t findAtom(struct bufio* b, int64_t start, int64_t end,
                        const char name[4], int64_t* bodySize)
{
	int64_t pos = start;

	while(pos + 8 <= end)
	{
		unsigned char	h[16];
		int64_t			size;
		int64_t			hdrLen = 8;

		if(bufioSeek(b, pos, SEEK_SET) != 0 || bufioRead(b, h, 8) != 8)
			return -1;

		size = be32(h);

		if(size == 1)
		{
			/* 64-bit size follows the type (common for large mdat). */
			if(bufioRead(b, h + 8, 8) != 8)
				return -1;
			size = ((int64_t)be32(h + 8) << 32) | be32(h + 12);
			hdrLen = 16;
		}
		else if(size == 0)
			size = end - pos;	/* Extends to the end of the container. */

		if(size < hdrLen || size > end - pos)
			return -1;

		if(memcmp(h + 4, name, 4) == 0)
		{
			*bodySize = size - hdrLen;
			return pos + hdrLen;
		}

		pos += size;
	}

	return -1;
}

static void parseM4a(struct bufio* b, struct music_metadata* meta)
{
	int64_t sz, end;
	int64_t moov, udta, metaAtom, ilst;

	if((moov = findAtom(b, 0, bufioSize(b), "moov", &sz)) < 0)
		return;
	end = moov + sz;

	if((udta = findAtom(b, moov, end, "udta", &sz)) < 0)
		return;
	end = udta + sz;

	if((metaAtom = findAtom(b, udta, end, "meta", &sz)) < 0)
		return;
	end = metaAtom + sz;

	/* 'meta' is a full box: 4 bytes version/flags precede its children. */
	if((ilst = findAtom(b, metaAtom + 4, end, "ilst", &sz)) < 0)
		return;
	end = ilst + sz;

	for(int64_t pos = ilst; pos + 8 <= end && !metaComplete(meta);)
	{
		unsigned char	h[8];
		int64_t			itemSize;
		enum meta_field	f = FIELD_NONE;

		if(bufioSeek(b, pos, SEEK_SET) != 0 || bufioRead(b, h, 8) != 8)
			return;

		itemSize = be32(h);
		if(itemSize < 8 || itemSize > end - pos)
			return;

		/* (c)nam, (c)ART, (c)alb */
		if(h[4] == 0xA9)
		{
			if(memcmp(h + 5, "nam", 3) == 0) f = FIELD_TITLE;
			else if(memcmp(h + 5, "ART", 3) == 0) f = FIELD_ARTIST;
			else if(memcmp(h + 5, "alb", 3) == 0) f = FIELD_ALBUM;
		}

		if(f != FIELD_NONE)
		{
			int64_t dataSize;
			int64_t data = findAtom(b, pos + 8, pos + itemSize, "data",
			                        &dataSize);

			/* Skip 4 bytes type indicator + 4 bytes locale. */
			if(data >= 0 && dataSize > 8 && dataSize - 8 < 512)
			{
				char	val[512];
				size_t	valLen = (size_t)(dataSize - 8);

				if(bufioSeek(b, data + 8, SEEK_SET) == 0 &&
				   bufioRead(b, val, valLen) == valLen)
					setField(meta, f, val, valLen);
			}
		}

		pos += itemSize;
	}
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int getMetadata(const char* file, struct music_metadata* meta)
{
	struct bufio	b;
	unsigned char	magic[8];
	size_t			got;

	if(file == NULL || meta == NULL)
		return -1;

	memset(meta, 0, sizeof(*meta));

	if(getFileTypeFromExt(file) == FILE_TYPE_ERROR)
		return -1;

	if(bufioOpen(&b, file, META_BUF_SIZE) != 0)
		return -1;

	/* Dispatch on content rather than extension, so e.g. an Opus file
	 * named .ogg or a FLAC with an ID3 tag still works. */
	got = bufioRead(&b, magic, sizeof(magic));
	bufioSeek(&b, 0, SEEK_SET);

	if(got >= 4 && memcmp(magic, "OggS", 4) == 0)
		parseOgg(&b, meta);
	else if(got >= 4 && memcmp(magic, "fLaC", 4) == 0)
		parseFlac(&b, meta);
	else if(got >= 8 && memcmp(magic + 4, "ftyp", 4) == 0)
		parseM4a(&b, meta);
	else if(got >= 3 && memcmp(magic, "ID3", 3) == 0)
	{
		unsigned char next[4];

		parseId3v2(&b, meta);

		/* Some FLAC files carry a leading ID3 tag. */
		if(bufioRead(&b, next, 4) == 4 && memcmp(next, "fLaC", 4) == 0)
		{
			bufioSeek(&b, -4, SEEK_CUR);
			parseFlac(&b, meta);
		}
		else if(getFileTypeFromExt(file) == FILE_TYPE_MP3 &&
		        !metaComplete(meta))
			parseId3v1(&b, meta);
	}
	else if(getFileTypeFromExt(file) == FILE_TYPE_MP3)
		parseId3v1(&b, meta);

	bufioClose(&b);
	return metaAny(meta) ? 0 : -1;
}

void getDisplayTitle(const char* filepath, char* out, size_t outLen)
{
	struct music_metadata meta;

	if(filepath == NULL || out == NULL || outLen == 0)
		return;

	out[0] = '\0';

	if(getMetadata(filepath, &meta) == 0 && meta.has_title &&
	   meta.title[0] != '\0')
	{
		strncpy(out, meta.title, outLen - 1);
		out[outLen - 1] = '\0';
		return;
	}

	/* Fall back to filename (strip path) */
	const char* slash = strrchr(filepath, '/');
	const char* name = slash ? slash + 1 : filepath;
	strncpy(out, name, outLen - 1);
	out[outLen - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* Cover art                                                          */
/* ------------------------------------------------------------------ */

/* Bigger embedded pictures are ignored (they'd only be scaled down to
 * 144x144 anyway, and have to fit in RAM as one blob). */
#define ART_MAX_BYTES		(8 * 1024 * 1024)
/* ID3 / FLAC picture type for "Cover (front)". */
#define PIC_FRONT_COVER		3

struct artResult
{
	unsigned char*	data;	/* image file bytes (JPEG/PNG/...) */
	size_t			len;
	int				type;	/* picture type, -1 if unknown */
};

static bool artDone(const struct artResult* r)
{
	return r->data != NULL && r->type == PIC_FRONT_COVER;
}

/**
 * Consider a picture found in buf (image bytes at buf + off, len long):
 * keep it if there is none yet, or if it is the front cover and the kept
 * one isn't. Takes ownership of buf either way.
 */
static void artOffer(struct artResult* r, unsigned char* buf, size_t off,
                     size_t len, int type)
{
	if(len == 0 || (r->data != NULL && (r->type == PIC_FRONT_COVER ||
	                                    type != PIC_FRONT_COVER)))
	{
		free(buf);
		return;
	}

	free(r->data);
	memmove(buf, buf + off, len);
	r->data = buf;
	r->len = len;
	r->type = type;
}

/** Read n bytes into a new buffer (NULL if too big or on a short read). */
static unsigned char* artReadAlloc(const struct byteSource* src, size_t n)
{
	unsigned char* buf;

	if(n == 0 || n > ART_MAX_BYTES || (buf = malloc(n)) == NULL)
		return NULL;

	if(src->read(src->ctx, buf, n) != n)
	{
		free(buf);
		return NULL;
	}
	return buf;
}

/**
 * Parse a FLAC METADATA_BLOCK_PICTURE structure (also what the Ogg
 * METADATA_BLOCK_PICTURE comment holds, base64-encoded):
 * type, MIME length + MIME, description length + description, width,
 * height, depth, colours, data length + data. All big-endian 32-bit.
 */
static void artFromFlacPicture(struct artResult* r, unsigned char* buf,
                               size_t len)
{
	size_t		p;
	uint32_t	type, n;

	if(len < 32)
		goto bad;

	type = be32(buf);
	p = 4;

	n = be32(buf + p);					/* MIME type */
	p += 4;
	if(n > len - p || len - p - n < 4)
		goto bad;
	p += n;

	n = be32(buf + p);					/* description */
	p += 4;
	if(n > len - p || len - p - n < 20)
		goto bad;
	p += n + 16;						/* + width/height/depth/colours */

	n = be32(buf + p);					/* picture data */
	p += 4;
	if(n > len - p)
		goto bad;

	artOffer(r, buf, p, n, (int)type);
	return;

bad:
	free(buf);
}

/** Decode base64 in place (ignores whitespace). Returns the decoded size. */
static size_t base64Decode(unsigned char* s, size_t len)
{
	uint32_t	acc = 0;
	int			bits = 0;
	size_t		o = 0;

	for(size_t i = 0; i < len; i++)
	{
		unsigned char	c = s[i];
		int				v;

		if(c >= 'A' && c <= 'Z')		v = c - 'A';
		else if(c >= 'a' && c <= 'z')	v = c - 'a' + 26;
		else if(c >= '0' && c <= '9')	v = c - '0' + 52;
		else if(c == '+' || c == '-')	v = 62;
		else if(c == '/' || c == '_')	v = 63;
		else if(c == '=')				break;
		else							continue;

		acc = (acc << 6) | (uint32_t)v;
		bits += 6;
		if(bits >= 8)
		{
			bits -= 8;
			s[o++] = (unsigned char)(acc >> bits);
		}
	}

	return o;
}

/* ID3v2 APIC (v2.3/2.4) or PIC (v2.2) frames. */
static void artId3v2(struct bufio* b, struct artResult* r)
{
	unsigned char	h[10];
	int64_t			tagStart = bufioTell(b);
	int64_t			tagEnd;
	int				version;
	bool			tagUnsync;
	size_t			frameHdrLen;

	if(bufioRead(b, h, 10) != 10 || memcmp(h, "ID3", 3) != 0)
		return;

	version = h[3];
	tagUnsync = (h[5] & 0x80) != 0;
	tagEnd = tagStart + 10 + (int64_t)syncsafe(h + 6);
	if(h[5] & 0x10)
		tagEnd += 10;

	if(version < 2 || version > 4 || (version == 2 && (h[5] & 0x40)))
		goto done;

	if(version >= 3 && (h[5] & 0x40))
	{
		unsigned char e[4];

		if(bufioRead(b, e, 4) != 4)
			goto done;
		bufioSeek(b, (int64_t)((version == 3) ? be32(e) : syncsafe(e) - 4),
		          SEEK_CUR);
	}

	frameHdrLen = (version == 2) ? 6 : 10;

	while(!artDone(r) && bufioTell(b) + (int64_t)frameHdrLen <= tagEnd)
	{
		unsigned char	fh[10];
		unsigned char*	buf;
		uint32_t		sz;
		unsigned		fflags = 0;
		int64_t			frameEnd;
		size_t			off = 0, len, i;
		bool			unsync = tagUnsync;
		bool			isPic;
		int				type;

		if(bufioRead(b, fh, frameHdrLen) != frameHdrLen || fh[0] == 0)
			break;

		if(version == 2)
			sz = be24(fh + 3);
		else
		{
			sz = (version == 4) ? syncsafe(fh + 4) : be32(fh + 4);
			fflags = fh[9];
		}

		frameEnd = bufioTell(b) + (int64_t)sz;
		if(frameEnd > tagEnd)
			break;

		isPic = (version == 2) ? memcmp(fh, "PIC", 3) == 0 :
		                         memcmp(fh, "APIC", 4) == 0;
		if(!isPic || sz < 4 || sz > ART_MAX_BYTES ||
		   (version == 3 && (fflags & 0xC0)) ||
		   (version == 4 && (fflags & 0x0C)))
		{
			bufioSeek(b, frameEnd, SEEK_SET);
			continue;
		}

		if((buf = malloc(sz)) == NULL)
			break;
		if(bufioRead(b, buf, sz) != sz)
		{
			free(buf);
			break;
		}

		if(version == 3 && (fflags & 0x20))
			off += 1;
		else if(version == 4)
		{
			if(fflags & 0x40)
				off += 1;
			if(fflags & 0x01)
				off += 4;
			if(fflags & 0x02)
				unsync = true;
		}

		if(off >= sz)
		{
			free(buf);
			continue;
		}

		len = sz - off;
		if(unsync)
			len = id3Deunsync(buf + off, len);

		/* encoding, MIME (v2.2: 3-char format), type, description, data */
		{
			const unsigned char* p = buf + off;
			int enc = p[0];

			i = 1;
			if(version == 2)
				i += 3;
			else
			{
				while(i < len && p[i] != 0)
					i++;
				i++;
			}

			if(i >= len)
			{
				free(buf);
				continue;
			}
			type = p[i++];

			if(enc == 1 || enc == 2)
			{
				/* UTF-16: ends with an aligned 00 00 */
				while(i + 1 < len && !(p[i] == 0 && p[i + 1] == 0))
					i += 2;
				i += 2;
			}
			else
			{
				while(i < len && p[i] != 0)
					i++;
				i++;
			}

			if(i >= len)
			{
				free(buf);
				continue;
			}
		}

		artOffer(r, buf, off + i, len - i, type);
	}

done:
	bufioSeek(b, tagEnd, SEEK_SET);
}

/* Native FLAC: PICTURE metadata blocks (type 6). */
static void artFlac(struct bufio* b, struct artResult* r)
{
	const struct byteSource	src = { bufioSourceRead, b };
	unsigned char			hdr[4];

	if(bufioRead(b, hdr, 4) != 4 || memcmp(hdr, "fLaC", 4) != 0)
		return;

	for(int blocks = 0; blocks < 128 && !artDone(r); blocks++)
	{
		bool		last;
		unsigned	type;
		uint32_t	len;
		int64_t		next;

		if(bufioRead(b, hdr, 4) != 4)
			return;

		last = (hdr[0] & 0x80) != 0;
		type = hdr[0] & 0x7F;
		len = be24(hdr + 1);
		next = bufioTell(b) + (int64_t)len;

		if(type == 6)
		{
			unsigned char* buf = artReadAlloc(&src, len);

			if(buf != NULL)
				artFromFlacPicture(r, buf, len);
		}

		if(last || type == 127)
			return;

		bufioSeek(b, next, SEEK_SET);
	}
}

/**
 * Vorbis comments: METADATA_BLOCK_PICTURE=<base64 FLAC picture> (the
 * standard), or the old COVERART=<base64 image>.
 */
static void artVorbisComments(const struct byteSource* src,
                              struct artResult* r)
{
	static const char	kPic[] = "METADATA_BLOCK_PICTURE=";
	static const char	kCover[] = "COVERART=";
	const size_t		kPicLen = sizeof(kPic) - 1;
	const size_t		kCoverLen = sizeof(kCover) - 1;
	unsigned char		n4[4];
	uint32_t			count;

	if(src->read(src->ctx, n4, 4) != 4)
		return;
	{
		uint32_t vendorLen = le32(n4);
		if(src->read(src->ctx, NULL, vendorLen) != vendorLen)
			return;
	}

	if(src->read(src->ctx, n4, 4) != 4)
		return;
	count = le32(n4);

	for(uint32_t i = 0; i < count && !artDone(r); i++)
	{
		char			key[sizeof(kPic) - 1];
		uint32_t		len;
		size_t			kl, rest;
		unsigned char*	buf;

		if(src->read(src->ctx, n4, 4) != 4)
			return;
		len = le32(n4);

		kl = len < kPicLen ? len : kPicLen;
		if(src->read(src->ctx, key, kl) != kl)
			return;
		rest = len - kl;

		if(kl == kPicLen && strncasecmp_local(key, kPic, kPicLen) == 0)
		{
			if(rest > ART_MAX_BYTES)
			{
				if(src->read(src->ctx, NULL, rest) != rest)
					return;
				continue;
			}
			if((buf = artReadAlloc(src, rest)) == NULL)
				return;
			artFromFlacPicture(r, buf, base64Decode(buf, rest));
		}
		else if(kl >= kCoverLen &&
		        strncasecmp_local(key, kCover, kCoverLen) == 0 &&
		        rest <= ART_MAX_BYTES)
		{
			size_t have = kl - kCoverLen;

			if((buf = malloc(have + rest + 1)) == NULL)
				return;
			memcpy(buf, key + kCoverLen, have);
			if(src->read(src->ctx, buf + have, rest) != rest)
			{
				free(buf);
				return;
			}
			artOffer(r, buf, 0, base64Decode(buf, have + rest), -1);
		}
		else if(src->read(src->ctx, NULL, rest) != rest)
			return;
	}
}

/* Ogg Vorbis / Opus (comment header) and Ogg FLAC (PICTURE packets). */
static void artOgg(struct bufio* b, struct artResult* r)
{
	struct oggReader		o;
	const struct byteSource	src = { oggPacketRead, &o };
	unsigned char			id[8];

	memset(&o, 0, sizeof(o));
	o.b = b;

	if(!oggNextPage(&o) || oggPacketRead(&o, id, sizeof(id)) < 5)
		return;

	if(memcmp(id, "\x01vorbis", 7) == 0)
	{
		unsigned char magic[7];
		if(oggNextPacket(&o) && oggPacketRead(&o, magic, 7) == 7 &&
		   memcmp(magic, "\x03vorbis", 7) == 0)
			artVorbisComments(&src, r);
	}
	else if(memcmp(id, "OpusHead", 8) == 0)
	{
		unsigned char magic[8];
		if(oggNextPacket(&o) && oggPacketRead(&o, magic, 8) == 8 &&
		   memcmp(magic, "OpusTags", 8) == 0)
			artVorbisComments(&src, r);
	}
	else if(memcmp(id, "\x7F" "FLAC", 5) == 0)
	{
		for(int i = 0; i < 16 && !artDone(r) && oggNextPacket(&o); i++)
		{
			unsigned char hdr[4];

			if(oggPacketRead(&o, hdr, 4) != 4)
				return;

			if((hdr[0] & 0x7F) == 6)
			{
				uint32_t		len = be24(hdr + 1);
				unsigned char*	buf = artReadAlloc(&src, len);

				if(buf != NULL)
					artFromFlacPicture(r, buf, len);
			}
			else if((hdr[0] & 0x7F) == 4)
			{
				/* Pictures can also hide in the comments. */
				artVorbisComments(&src, r);
			}

			if(hdr[0] & 0x80)
				return;
		}
	}
}

/* M4A: moov/udta/meta/ilst/covr/data. */
static void artM4a(struct bufio* b, struct artResult* r)
{
	int64_t sz, end, pos;

	if((pos = findAtom(b, 0, bufioSize(b), "moov", &sz)) < 0)
		return;
	end = pos + sz;
	if((pos = findAtom(b, pos, end, "udta", &sz)) < 0)
		return;
	end = pos + sz;
	if((pos = findAtom(b, pos, end, "meta", &sz)) < 0)
		return;
	end = pos + sz;
	if((pos = findAtom(b, pos + 4, end, "ilst", &sz)) < 0)
		return;
	end = pos + sz;
	if((pos = findAtom(b, pos, end, "covr", &sz)) < 0)
		return;
	end = pos + sz;

	/* The first 'data' child: 4 bytes type (13 JPEG, 14 PNG), 4 locale. */
	if((pos = findAtom(b, pos, end, "data", &sz)) < 0 || sz <= 8 ||
	   sz - 8 > ART_MAX_BYTES)
		return;

	{
		size_t			len = (size_t)(sz - 8);
		unsigned char*	buf = malloc(len);

		if(buf == NULL)
			return;
		if(bufioSeek(b, pos + 8, SEEK_SET) != 0 ||
		   bufioRead(b, buf, len) != len)
		{
			free(buf);
			return;
		}
		artOffer(r, buf, 0, len, PIC_FRONT_COVER);
	}
}

int getCoverArt(const char* file, unsigned char** data, size_t* len)
{
	struct bufio		b;
	struct artResult	r = { NULL, 0, -1 };
	unsigned char		magic[8];
	size_t				got;

	if(file == NULL || data == NULL || len == NULL)
		return -1;

	*data = NULL;
	*len = 0;

	if(bufioOpen(&b, file, 64 * 1024) != 0)
		return -1;

	got = bufioRead(&b, magic, sizeof(magic));
	bufioSeek(&b, 0, SEEK_SET);

	if(got >= 4 && memcmp(magic, "OggS", 4) == 0)
		artOgg(&b, &r);
	else if(got >= 4 && memcmp(magic, "fLaC", 4) == 0)
		artFlac(&b, &r);
	else if(got >= 8 && memcmp(magic + 4, "ftyp", 4) == 0)
		artM4a(&b, &r);
	else if(got >= 3 && memcmp(magic, "ID3", 3) == 0)
	{
		unsigned char next[4];

		artId3v2(&b, &r);

		/* FLAC with a leading ID3 tag: its own PICTURE blocks too. */
		if(!artDone(&r) && bufioRead(&b, next, 4) == 4 &&
		   memcmp(next, "fLaC", 4) == 0)
		{
			bufioSeek(&b, -4, SEEK_CUR);
			artFlac(&b, &r);
		}
	}

	bufioClose(&b);

	if(r.data == NULL)
		return -1;

	*data = r.data;
	*len = r.len;
	return 0;
}
