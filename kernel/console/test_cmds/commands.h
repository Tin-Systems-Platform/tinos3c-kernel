#ifndef CONSOLE_COMMANDS_H
#define CONSOLE_COMMANDS_H

/** Display physical memory usage statistics.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void console_cmd_meminfo(void);

/** Run disk-management helpers for a complete disk command line.
 * @param command Mutable, null-terminated line beginning with "disk".
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void console_cmd_disk(char *command);

/** List registered block devices and their capacities.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void console_cmd_massstorageinfo(void);

/** Parse and run the disktest arguments.
 * @param arguments Null-terminated text after "disktest".
 * @return None; prints usage or test status.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void console_cmd_disktest(const char *arguments);

#endif
