#ifndef QUIRK_OS_GUESSING_H_
#define QUIRK_OS_GUESSING_H_

typedef enum {
  QUIRK_OS_GUESSING_UNKNOWN,
  QUIRK_OS_GUESSING_LINUX,
  QUIRK_OS_GUESSING_OSX,
  QUIRK_OS_GUESSING_WINDOWS,
} quirk_os_guessing_t;

quirk_os_guessing_t quirk_os_guessing_get(void);
void quirk_os_guessing_desc_device_cb(void);
void quirk_os_guessing_desc_configuration_cb(void);
void quirk_os_guessing_desc_bos_cb(void);
void quirk_os_guessing_desc_string_cb(void);

#endif
