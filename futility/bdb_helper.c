/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Boot descriptor block helper functions
 */

#include <inttypes.h>
#include <stdio.h>

#include "2sha.h"
#include "bdb.h"
#include "bdb_struct.h"
#include "file_type.h"

enum futil_file_type ft_recognize_bdb(uint8_t *buf, uint32_t len)
{
	const struct bdb_header *header = bdb_get_header(buf);

	if (bdb_check_header(header, len))
		return FILE_TYPE_UNKNOWN;

	return FILE_TYPE_BDB;
}

static void print_digest(const char *label, const uint8_t *digest, size_t size)
{
	int i;

	if (label)
		printf("%s", label);
	for (i = 0; i < size; i++)
		printf("%02x", digest[i]);
	printf("\n");
}

static void print_hash_entry(const char *label, const struct bdb_hash *hash)
{
	if (label)
		printf("%s", label);
	printf("  Offset:       0x%" PRIx64 "\n", hash->offset);
	printf("  Size:         %d\n", hash->size);
	printf("  Partition:    %d\n", hash->partition);
	printf("  Type:         %d\n", hash->type);
	printf("  Load Address: 0x%" PRIx64 "\n", hash->load_address);
	print_digest("  Digest:       ", hash->digest, sizeof(hash->digest));
}

static void show_bdb_header(const uint8_t *bdb)
{
	const struct bdb_header *header = bdb_get_header(bdb);
	const struct bdb_key *key = bdb_get_bdbkey(bdb);
	uint8_t digest[BDB_SHA256_DIGEST_SIZE];

	printf("BDB Header:\n");
	printf("  Struct Version: 0x%x:0x%x\n",
	       header->struct_major_version, header->struct_minor_version);

	bdb_sha256(digest, key, key->struct_size);
	print_digest("  BDB key digest: ", digest, sizeof(digest));
	printf("            size: %d\n", key->struct_size);
}

static void show_hashes(const uint8_t *bdb)
{
	const struct bdb_data *data = bdb_get_data(bdb);
	int i;

	for (i = 0; i < data->num_hashes; i++) {
		const struct bdb_hash *hash = bdb_get_hash_by_index(bdb, i);
		printf("Hash #%d:\n", i);
		print_hash_entry(NULL, hash);
	}
}

int ft_show_bdb(const char *name, uint8_t *buf, uint32_t len, void *data)
{
	const struct bdb_header *header = bdb_get_header(buf);
	int rv;

	/* We can get here because of '--type' option */
	rv  = bdb_check_header(header, len);
	if (rv) {
		fprintf(stderr, "ERROR: Invalid BDB blob: %d\n", rv);
		return 1;
	}

	printf("Boot Descriptor Block: %s\n", name);
	show_bdb_header(buf);
	show_hashes(buf);

	return 0;
}