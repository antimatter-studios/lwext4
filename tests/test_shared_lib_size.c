/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * The toolchain files define SIZE, which adds a lib_size target that prints
 * the size of the library after every build. It ran "size liblwext4.a" even
 * for the shared library (LWEXT4_BUILD_SHARED_LIB=ON), so that build always
 * failed. test_shared_lib_size.sh builds the shared library; here its build
 * output has to show the size of the library that was built.
 */

#include "test_util.h"

#include <string.h>

int main(int argc, char **argv)
{
	const char *log = test_image_arg(argc, argv);
	static char buf[1 << 16];
	const char *header, *lib;
	size_t len;
	FILE *f;

	f = fopen(log, "rb");
	TEST_ASSERT(f != NULL);
	len = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[len] = '\0';

	/* size(1): "text data bss dec hex filename", then one line per file */
	header = strstr(buf, "filename");
	TEST_ASSERT(header != NULL);
	lib = strstr(header, "liblwext4");
	TEST_ASSERT(lib != NULL);
	TEST_ASSERT(strncmp(lib, "liblwext4.a", 11) != 0);
	return 0;
}
