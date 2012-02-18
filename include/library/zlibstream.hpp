#ifndef _library__zlibstream__hpp__included__
#define _library__zlibstream__hpp__included__

#include <vector>
#include <list>
#include <cstdint>
#include <zlib.h>
#define ZLIB_PAGE_STORAGE 65500

/**
 * Zlib stream.
 */
struct zlibstream
{
/**
 * Create new zlib stream.
 *
 * Parameter compression: The compression level.
 */
	zlibstream(unsigned compression);
/**
 * Destroy zlib stream.
 */
	~zlibstream();
/**
 * Reset the stream, adding some initial data.
 *
 * Parameter data: The data to write (may be NULL if datalen=0).
 * Parameter datalen: The length of data.
 */
	void reset(uint8_t* data, size_t datalen);
/**
 * Write data into stream.
 *
 * Parameter data: The data to write.
 * Parameter datalen: The length of data.
 */
	void write(uint8_t* data, size_t datalen);
/**
 * Read the data as buffer.
 *
 * Parameter out: The buffer to write to.
 */
	void read(std::vector<char>& out);
/**
 * Set flag.
 *
 * Parameter f: New flag value.
 */
	void set_flag(bool f);
/**
 * Get flag.
 *
 * Returns: Flag value.
 */
	bool get_flag();
private:
	struct page
	{
		uint8_t data[ZLIB_PAGE_STORAGE];
		size_t used;
	};

	void flushpage(uint8_t* data, size_t datalen, bool final);
	zlibstream(zlibstream&);
	zlibstream& operator=(zlibstream&);
	z_stream z;
	size_t streamsize;
	std::list<page> storage;
	bool flag;
};

#endif
