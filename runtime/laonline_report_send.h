#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void laonline_report_configure(const char *https_url);
int laonline_report_endpoint(char *out,size_t cap);
int laonline_report_send(const char *reviewed_json);
int laonline_report_status(void); // 0 idle, 1 sending, 2 confirmed, -1 failed
void laonline_report_clear_status(void);
void laonline_report_message(char *out,size_t cap); // V12.5: why the last send failed (plain language)
/* V13.5: one screenshot or video sent with the next report (PNG, JPG, MP4, MOV, WEBM, up to 9 MB - Discord's limit).
 * Returns 1 if accepted; otherwise *why says why. Empty path removes it. */
int laonline_report_attach(const char *path,char *why,size_t cap);
int laonline_report_attachment(char *name,size_t cap,long long *bytes); // 1 when one is attached
#ifdef __cplusplus
}
#endif
