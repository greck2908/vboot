/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * A reference implementation for AP (and supporting images) firmware updater.
 */
#ifndef VBOOT_REFERENCE_FUTILITY_UPDATER_H_
#define VBOOT_REFERENCE_FUTILITY_UPDATER_H_

#include <stdio.h>

#include "fmap.h"

extern int debugging_enabled;
#define DEBUG(format, ...) do { if (debugging_enabled) fprintf(stderr, \
	"DEBUG: %s: " format "\n", __FUNCTION__, ##__VA_ARGS__); } while (0)
#define ERROR(format, ...) fprintf(stderr, \
	"ERROR: %s: " format "\n", __FUNCTION__, ##__VA_ARGS__)
#define ASPRINTF(strp, ...) do { if (asprintf(strp, __VA_ARGS__) >= 0) break; \
	ERROR("Failed to allocate memory, abort."); exit(1); } while (0)

/* FMAP section names. */
static const char * const FMAP_RO_FRID = "RO_FRID",
		  * const FMAP_RO_SECTION = "RO_SECTION",
		  * const FMAP_RO_GBB = "GBB",
		  * const FMAP_RO_PRESERVE = "RO_PRESERVE",
		  * const FMAP_RO_VPD = "RO_VPD",
		  * const FMAP_RW_VPD = "RW_VPD",
		  * const FMAP_RW_VBLOCK_A = "VBLOCK_A",
		  * const FMAP_RW_SECTION_A = "RW_SECTION_A",
		  * const FMAP_RW_SECTION_B = "RW_SECTION_B",
		  * const FMAP_RW_FWID = "RW_FWID",
		  * const FMAP_RW_FWID_A = "RW_FWID_A",
		  * const FMAP_RW_FWID_B = "RW_FWID_B",
		  * const FMAP_RW_SHARED = "RW_SHARED",
		  * const FMAP_RW_NVRAM = "RW_NVRAM",
		  * const FMAP_RW_ELOG = "RW_ELOG",
		  * const FMAP_RW_PRESERVE = "RW_PRESERVE",
		  * const FMAP_RW_LEGACY = "RW_LEGACY",
		  * const FMAP_RW_SMMSTORE = "SMMSTORE",
		  * const FMAP_SI_DESC = "SI_DESC",
		  * const FMAP_SI_ME = "SI_ME";

struct firmware_image {
	const char *programmer;
	uint32_t size;
	uint8_t *data;
	char *file_name;
	char *ro_version, *rw_version_a, *rw_version_b;
	FmapHeader *fmap_header;
};

struct firmware_section {
	uint8_t *data;
	size_t size;
};

struct system_property {
	int (*getter)();
	int value;
	int initialized;
};

enum system_property_type {
	SYS_PROP_MAINFW_ACT,
	SYS_PROP_TPM_FWVER,
	SYS_PROP_FW_VBOOT2,
	SYS_PROP_PLATFORM_VER,
	SYS_PROP_WP_HW,
	SYS_PROP_WP_SW,
	SYS_PROP_MAX
};

struct updater_config;
struct quirk_entry {
	const char *name;
	const char *help;
	int (*apply)(struct updater_config *cfg);
	int value;
};

enum quirk_types {
	QUIRK_ENLARGE_IMAGE,
	QUIRK_MIN_PLATFORM_VERSION,
	QUIRK_UNLOCK_ME_FOR_UPDATE,
	QUIRK_DAISY_SNOW_DUAL_MODEL,
	QUIRK_EVE_SMM_STORE,
	QUIRK_MAX,
};

struct tempfile {
	char *filepath;
	struct tempfile *next;
};

struct archive;
struct updater_config {
	struct firmware_image image, image_current;
	struct firmware_image ec_image, pd_image;
	struct system_property system_properties[SYS_PROP_MAX];
	struct quirk_entry quirks[QUIRK_MAX];
	struct archive *archive;
	struct tempfile *tempfiles;
	int try_update;
	int force_update;
	int legacy_update;
	int verbosity;
	const char *emulation;
};

struct updater_config_arguments {
	char *image, *ec_image, *pd_image;
	char *archive, *quirks, *mode;
	char *programmer, *model;
	char *emulation, *sys_props, *write_protection;
	int is_factory, try_update, force_update, do_manifest;
	int verbosity;
};

struct model_config {
	char *name;
	char *image, *ec_image, *pd_image;
	char *signature_id;
};

struct manifest {
	int num;
	int default_model;
	int has_keyset;
	struct model_config *models;
	struct archive *archive;
};

enum updater_error_codes {
	UPDATE_ERR_DONE,
	UPDATE_ERR_NEED_RO_UPDATE,
	UPDATE_ERR_NO_IMAGE,
	UPDATE_ERR_SYSTEM_IMAGE,
	UPDATE_ERR_INVALID_IMAGE,
	UPDATE_ERR_SET_COOKIES,
	UPDATE_ERR_WRITE_FIRMWARE,
	UPDATE_ERR_PLATFORM,
	UPDATE_ERR_TARGET,
	UPDATE_ERR_ROOT_KEY,
	UPDATE_ERR_TPM_ROLLBACK,
	UPDATE_ERR_UNKNOWN,
};

/* Messages explaining enum updater_error_codes. */
extern const char * const updater_error_messages[];

struct updater_config;

/*
 * The main updater to update system firmware using the configuration parameter.
 * Returns UPDATE_ERR_DONE if success, otherwise failure.
 */
enum updater_error_codes update_firmware(struct updater_config *cfg);

/*
 * Allocates and initializes a updater_config object with default values.
 * Returns the newly allocated object, or NULL on error.
 */
struct updater_config *updater_new_config();

/*
 * Releases all resources in an updater configuration object.
 */
void updater_delete_config(struct updater_config *cfg);

/*
 * Helper function to setup an allocated updater_config object.
 * Returns number of failures, or 0 on success.
 */
int updater_setup_config(struct updater_config *cfg,
			 const struct updater_config_arguments *arg);

/* Prints the name and description from all supported quirks. */
void updater_list_config_quirks(const struct updater_config *cfg);

/*
 * Registers known quirks to a updater_config object.
 */
void updater_register_quirks(struct updater_config *cfg);

/*
 * Helper function to create a new temporary file within updater's life cycle.
 * Returns the path of new file, or NULL on failure.
 */
const char *updater_create_temp_file(struct updater_config *cfg);

/*
 * Finds a firmware section by given name in the firmware image.
 * If successful, return zero and *section argument contains the address and
 * size of the section; otherwise failure.
 */
int find_firmware_section(struct firmware_section *section,
			  const struct firmware_image *image,
			  const char *section_name);

/*
 * Preserves (copies) the given section (by name) from image_from to image_to.
 * The offset may be different, and the section data will be directly copied.
 * If the section does not exist on either images, return as failure.
 * If the source section is larger, contents on destination be truncated.
 * If the source section is smaller, the remaining area is not modified.
 * Returns 0 if success, non-zero if error.
 */
int preserve_firmware_section(const struct firmware_image *image_from,
			      struct firmware_image *image_to,
			      const char *section_name);

/*
 * Loads a firmware image from file.
 * If archive is provided and file_name is a relative path, read the file from
 * archive.
 * Returns 0 on success, otherwise failure.
 */
int load_firmware_image(struct firmware_image *image, const char *file_name,
			struct archive *archive);

/*
 * Loads the active system firmware image (usually from SPI flash chip).
 * Returns 0 if success, non-zero if error.
 */
int load_system_firmware(struct updater_config *cfg,
			 struct firmware_image *image);

/* Frees the allocated resource from a firmware image object. */
void free_firmware_image(struct firmware_image *image);

/* Gets the value (setting) of specified quirks from updater configuration. */
int get_config_quirk(enum quirk_types quirk, const struct updater_config *cfg);

/* Gets the system property by given type. Returns the property value. */
int get_system_property(enum system_property_type property_type,
			struct updater_config *cfg);
/*
 * Gets the default quirk config string for target image.
 * Returns a string (in same format as --quirks) to load or NULL if no quirks.
 */
const char * const updater_get_default_quirks(struct updater_config *cfg);

/*
 * Executes a command on current host and returns stripped command output.
 * If the command has failed (exit code is not zero), returns an empty string.
 * The caller is responsible for releasing the returned string.
 */
char *host_shell(const char *command);

/* Functions from updater_archive.c */

/*
 * Opens an archive from given path.
 * The type of archive will be determined automatically.
 * Returns a pointer to reference to archive (must be released by archive_close
 * when not used), otherwise NULL on error.
 */
struct archive *archive_open(const char *path);

/*
 * Closes an archive reference.
 * Returns 0 on success, otherwise non-zero as failure.
 */
int archive_close(struct archive *ar);

/*
 * Checks if an entry (either file or directory) exists in archive.
 * Returns 1 if exists, otherwise 0
 */
int archive_has_entry(struct archive *ar, const char *name);

/*
 * Reads a file from archive.
 * Returns 0 on success (data and size reflects the file content),
 * otherwise non-zero as failure.
 */
int archive_read_file(struct archive *ar, const char *fname,
		      uint8_t **data, uint32_t *size);

/*
 * Creates a new manifest object by scanning files in archive.
 * Returns the manifest on success, otherwise NULL for failure.
 */
struct manifest *new_manifest_from_archive(struct archive *archive);

/* Releases all resources allocated by given manifest object. */
void delete_manifest(struct manifest *manifest);

/* Prints the information of objects in manifest (models and images) in JSON. */
void print_json_manifest(const struct manifest *manifest);

#endif  /* VBOOT_REFERENCE_FUTILITY_UPDATER_H_ */
