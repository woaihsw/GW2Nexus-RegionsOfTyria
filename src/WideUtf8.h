#ifndef WIDE_UTF8_H
#define WIDE_UTF8_H

// WideCharToMultiByte(..., cchWideChar = -1, ...) reports a size that includes
// the trailing NUL. The destination std::string must be that full size for the
// filling call; the logical payload is one byte shorter.
inline int utf8DestCapacityForWideCharSize(int sizeIncludingNull) {
	return sizeIncludingNull > 0 ? sizeIncludingNull : 0;
}

inline int utf8PayloadLengthFromWideCharWritten(int writtenIncludingNull) {
	return writtenIncludingNull > 0 ? writtenIncludingNull - 1 : 0;
}

#endif
