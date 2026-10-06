/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The wire of Keiland's system extension (WS135, plan/ws135/design.md
 * section 4.2; WS131 section 4): the opcodes of kl_system_manager_v1 and
 * kl_system_settings_v1, and the values their events carry.  The
 * compositor serves them and libkeiland speaks them; both include this
 * header and neither the other's code (WS131 D4 (c)).
 *
 * kl_system_manager_v1 (a global, version 10; its objects are made at its version)
 *   request 0 destroy
 *   request 1 get_settings(new_id kl_system_settings_v1)
 *   request 2 get_network(new_id kl_system_network_v1)    (WS131 p010)
 *   request 3 get_audio(new_id kl_system_audio_v1)
 *   request 4 get_power(new_id kl_system_power_v1)
 *   request 5 get_devices(new_id kl_system_devices_v1)
 *   request 6 get_monitor(new_id kl_system_monitor_v1, uint period_ms)    since version 2 (WS134 p012)
 *   request 7 get_account(new_id kl_system_account_v1)    since version 4 (ws160-p002)
 *   request 8 get_sharing(new_id kl_system_sharing_v1)    since version 7 (ws089-p025)
 *   event   0 capabilities(uint bits)              sent when it is bound
 *
 * kl_system_settings_v1
 *   request 0 destroy
 *   request 1 set(uint request, string key, string value)
 *   request 2 reset(uint request, string key)       back to the default
 *   event   0 value(string key, string value, uint flags)
 *   event   1 done(uint serial)                     the values before it are one state
 *   event   2 result(uint request, uint applied, uint saved)
 *
 * When made, a settings object hears every compositor setting's value and
 * a done; afterwards each change made by anyone (a client, the system bar,
 * audiod) comes to every settings object as its value and a done.  Every
 * set and reset is answered by one result.
 *
 * kl_system_network_v1 (WS131 p010)
 *   request 0 destroy
 *   request 1 request(uint request, uint what, string ssid)   scan, join (ssid), disconnect, Wi-Fi on, off
 *   request 2 save_key(uint request, string ssid, string key) saves the key, tells the daemon, joins
 *   request 3 query_details(uint request)                      the links, the DNS servers, the saved networks
 *   request 4 set_scanning(uint on)                            since version 3 (ws089-p021): the client shows the
 *                                                              networks around (1) or no longer (0)
 *   request 5 configure_wired(uint request, string interface, uint mode, string address, string netmask,
 *                             string router, string dns1, string dns2)
 *                                                              since version 6 (ws089-p022): a wired interface by
 *                                                              DHCP (mode 1; the strings empty but the DNS servers,
 *                                                              which may name static ones) or a static IPv4
 *                                                              address (mode 2; router and DNS may be empty), kept
 *                                                              by the network daemon for the next start too
 *   event   0 state(uint reachable, uint connected, uint kind, string interface, string wired, uint wifi,
 *                   string wifi_interface, string ssid)
 *   event   1 access_point(string ssid, int rssi, uint secured)
 *   event   2 scan_done()                           the access points before it are the whole scan
 *   event   3 link(string name, uint flags, string address, string netmask, string hardware, uint mtu,
 *                  uint received_high, uint received_low, uint sent_high, uint sent_low)
 *   event   4 dns(string address)
 *   event   5 saved_network(string ssid)
 *   event   6 details_done()                        the links, servers and networks before it are the whole details
 *   event   7 done(uint serial)
 *   event   8 result(uint request, uint applied, uint saved)
 *   event   9 wired(string name, uint mode, string router)    since version 6: after a wired interface's link in
 *                                                              the details, how it is configured (KL_SYSTEM_WIRED_*)
 *                                                              and the router it was given (empty when none)
 *   event  10 link_speed(string name, uint mbps)  since version 12 (BUG-222): after an interface's link in the
 *                                                              details, the speed its driver last heard, in Mb/s
 *                                                              (not sent while it is not known)
 *   One request of the network is outstanding at a time, the system bar's
 *   included; another is answered busy.  query_details is no request of
 *   the daemon's: every object that asked hears the next reading.  A join
 *   (and save_key's join) is answered no_key, refused or unreachable for
 *   its own failures.  set_scanning is no request and has no answer: the
 *   compositor keeps the radios scanning while any object asked for it
 *   (or the system bar's menu is open), and a new scan comes as the
 *   access points and a scan_done; the object's going ends its asking, and
 *   so does a minute without set_scanning(1) again (a client renews it
 *   while it shows the networks).
 *
 * kl_system_sharing_v1 (ws089-p025: the Sharing page's Remote Login, sshd)
 *   request 0 destroy
 *   request 1 set_ssh(uint request, uint on)          turns Remote Login on (1) or off (0), now and at
 *                                                     every start; the result follows the new state
 *   request 2 query(uint request)                     reads the state again
 *   event   0 state(uint available, uint enabled, uint running, uint port, uint allowed, string fingerprint)
 *                                                     whether the system has it, starts it, runs it, its
 *                                                     port, whether this user may change it (root or wheel),
 *                                                     the host key's fingerprint ("SHA256:...", or empty)
 *   event   1 done(uint serial)
 *   event   2 result(uint request, uint applied, uint saved)
 *   A new object hears the state last known and a done, and the state is
 *   read again for it; every change comes to every object.
 *
 * kl_system_audio_v1
 *   request 0 destroy
 *   request 1 set_volume(uint request, uint left, uint right, uint muted)
 *   request 2 feedback(uint request)
 *   event   0 state(uint reachable, uint device, uint rate, uint channels, uint left, uint right, uint muted)
 *   event   1 done(uint serial)
 *   event   2 result(uint request, uint applied, uint saved)
 *
 * kl_system_power_v1
 *   request 0 destroy
 *   request 1 action(uint request, uint action)              1 power off, 2 restart, 3 suspend
 *   event   0 state(uint source, int percent, uint charging, uint actions)
 *   event   1 done(uint serial)
 *   event   2 result(uint request, uint applied, uint saved)
 *
 * kl_system_devices_v1 (WS132: the removable media, ws132-p004)
 *   request 0 destroy
 *   request 1 eject(uint request, string id)
 *   request 2 mount(uint request, string id)                  since version 5
 *   event   0 device(string id, uint kind, uint state, string name, string location)
 *   event   1 done(uint serial)
 *   event   2 result(uint request, uint applied, uint saved)
 *   event   3 busy(uint request, string program)              since version 5: before a busy result, the program
 *                                                             that keeps the volume from being ejected
 *   event   4 volume(string id, string fs, uint bytes_high, uint bytes_low)
 *                                                             since version 9 (ws132-p009): after each device, its
 *                                                             file system ("fat", "ufs") and size in bytes, which
 *                                                             a mount's confirmation shows
 *   The devices are the volumes volumed lists (zedBSD): kind 1 (removable storage), state the
 *   KL_SYSTEM_DEVICE_* bits (mounted; new: inserted and never mounted since), name the label (the
 *   disk's name without one), location where it is mounted ("" when it is not).  Each object hears
 *   the whole list and a done when it is made and whenever it changes (a device not in the list is
 *   gone).  A mount puts the volume under /media (nosuid, noexec, the user its owner); an eject
 *   unmounts it (busy while a program uses it).  Only the seat's user may (denied otherwise).
 *
 * kl_system_account_v1 (ws160-p002: the user's own account)
 *   request 0 destroy
 *   request 1 set_password(uint request, string current, string new)
 *   event   0 result(uint request, uint applied, uint saved)
 *   request 2 administer(uint request, string password, string operation)   since version 8 (ws089-p026)
 *   event   1 refused(uint request, string reason)                         since version 8 (ws089-p026)
 *   request 3 set_pin(uint request, string current, string pin)            since version 10 (ws163-p003)
 *   event   2 enrolled(uint pin, uint keys)                                since version 11 (ws172-p002)
 *   request 4 add_key(uint request, string password, string label, string pin)  since version 14 (ws172-p003)
 *   request 5 remove_key(uint request, string password, string ref)       since version 14 (ws172-p003)
 *   event   3 key(string ref, string label)                                since version 14 (ws172-p003)
 *   event   4 touch(uint request)                                          since version 14 (ws172-p003)
 *   The compositor changes the password of the user it runs as, through
 *   the system (zedBSD: passwd; elsewhere unsupported), on a thread of its
 *   own, and answers ok, denied (the current password is wrong), invalid
 *   (the new one breaks the system's rules, or a password is empty or
 *   longer than KL_SYSTEM_PASSWORD_MAX), unsupported, busy (one change is
 *   under way) or failed.  Neither password is logged or kept.  The object
 *   has no state, and so no done.
 *   administer carries an administrator's change of the people's accounts
 *   (docs/architecture/security.md): the caller's password and the
 *   operation's lines (the operation, then its arguments, each ended by a
 *   line end, at most KL_SYSTEM_OPERATION_MAX bytes), which the compositor
 *   gives the system's tool (zedBSD: /usr/libexec/account-admin) on the
 *   same thread, one change at a time.  A refusal comes as refused with
 *   the tool's word (not-administrator, bad-password, ...) before the
 *   result; the result is ok, or denied for not-administrator and
 *   bad-password, invalid for the other refusals, unsupported, busy or
 *   failed.  The manager's capabilities have KL_SYSTEM_CAPABILITY_ADMINISTER
 *   where the system has the tool.
 *   set_pin sets the six-digit PIN of the user the compositor runs as, or
 *   removes it when pin is empty: the compositor passes both to the
 *   session manager (zedBSD: sessiond's ENROLL pin or REMOVE pin, which
 *   /sbin/passkey carries out in /etc/passkey; ws172-p002).  The result is
 *   ok, denied (current is wrong; a refusal's word comes first as refused:
 *   bad-secret, locked, ...), invalid (pin is not six digits), unsupported
 *   (no session manager), busy (another request is under way) or failed.
 *   Neither is logged or kept.  The manager's capabilities have
 *   KL_SYSTEM_CAPABILITY_PIN where a session manager runs.
 *   enrolled tells whether the user has a PIN and how many security keys
 *   (version 11): sent when the object is made, as soon as the session
 *   manager has answered, and again after each change.  Until it comes
 *   neither is known.
 *   add_key registers the security key plugged in for the user (version
 *   14): its label (1 to KL_SYSTEM_KEY_LABEL_MAX bytes, no colon), the
 *   key's own PIN, checked by the user's password (zedBSD: sessiond's
 *   ENROLL fido2, /usr/libexec/passkey-fido2).  While the key waits to be
 *   touched, touch(request) comes; then a refusal's word (bad-secret,
 *   no-key, many-keys, key-locked, timeout, ...) and the result, as
 *   set_pin's.  remove_key removes one by the reference key gave.  Before
 *   each enrolled, an object of version 14 hears key(ref, label) for each
 *   of the user's keys.  Neither secret is logged or kept.
 *
 * kl_system_monitor_v1 (WS134 p012, plan/ws134/design.md section 1.3)
 *   request 0 destroy
 *   request 1 ack(uint serial)                       the sample of that serial is taken
 *   request 2 set_period(uint period_ms)             250 to 10000
 *   event   0 info(uint cpu_count, string host, uint generation_high, uint generation_low)
 *   event   1 device(uint kind, uint id_high, uint id_low, uint generation_high, uint generation_low, uint subkind,
 *                    string name, string driver)
 *   event   2 info_done(uint serial)               the info and devices before it are the whole info
 *   event   3 cpu(uint index, uint user_high, uint user_low, uint system_high, uint system_low, uint idle_high,
 *                 uint idle_low, uint other_high, uint other_low)
 *   event   4 memory(uint total_high, uint total_low, uint free_high, uint free_low, uint cache_high, uint cache_low,
 *                    uint reclaimable_high, uint reclaimable_low, uint swap_total_high, uint swap_total_low,
 *                    uint swap_used_high, uint swap_used_low)
 *   event   5 link(uint id_high, uint id_low, uint rx_high, uint rx_low, uint tx_high, uint tx_low, uint up)
 *   event   6 disk(uint id_high, uint id_low, uint read_ops_high, uint read_ops_low, uint write_ops_high,
 *                  uint write_ops_low, uint read_bytes_high, uint read_bytes_low, uint write_bytes_high,
 *                  uint write_bytes_low, uint read_ns_high, uint read_ns_low, uint write_ns_high, uint write_ns_low,
 *                  uint busy_ns_high, uint busy_ns_low)
 *   event   7 gpu(uint id_high, uint id_low, uint time_high, uint time_low, uint busy_high, uint busy_low,
 *                 uint memory_used_high, uint memory_used_low, uint memory_total_high, uint memory_total_low,
 *                 uint cur_mhz, uint max_mhz, int milli_celsius, uint milli_watts)
 *   event   8 sample_done(uint serial, uint time_high, uint time_low, uint valid_high, uint valid_low, uint cpu_hz,
 *                         int cpu_milli_celsius)
 *   The compositor samples the machine (libkeiland-backend's monitor area) on a thread of its own while a monitor
 *   object exists, as often as the shortest period asked (at most 4 a second).  A sample is the counters (they only
 *   grow; a client makes the rates) and the present values, its cpu, memory, link, disk and gpu events and a
 *   sample_done; the info comes first and again when the devices change (info, a device a GPU, disk or link,
 *   info_done).  A u64 travels as its high and low halves.  No sample is sent before the client acked the last
 *   one, nor when the client's queue has no room for a whole one: a sample is sent whole or not at all, and one
 *   skipped is not owed (the next counters cover it).
 *
 * Every object hears its first state and a done when it is made, and each
 * change afterwards as the changed events and a done.
 */

#ifndef KEILAND_KL_SYSTEM_PROTOCOL_H
#define KEILAND_KL_SYSTEM_PROTOCOL_H

/* The interfaces' names and versions. */
#define KL_SYSTEM_MANAGER_NAME			"kl_system_manager_v1"
#define KL_SYSTEM_MANAGER_VERSION		14U
#define KL_SYSTEM_SETTINGS_NAME			"kl_system_settings_v1"

/* kl_system_manager_v1's requests and event. */
#define KL_SYSTEM_MANAGER_DESTROY		0U
#define KL_SYSTEM_MANAGER_GET_SETTINGS		1U
#define KL_SYSTEM_MANAGER_GET_NETWORK		2U
#define KL_SYSTEM_MANAGER_GET_AUDIO		3U
#define KL_SYSTEM_MANAGER_GET_POWER		4U
#define KL_SYSTEM_MANAGER_GET_DEVICES		5U
#define KL_SYSTEM_MANAGER_GET_MONITOR		6U
#define KL_SYSTEM_MANAGER_GET_ACCOUNT		7U
#define KL_SYSTEM_MANAGER_GET_SHARING		8U
#define KL_SYSTEM_MANAGER_GET_NOTIFY		9U
#define KL_SYSTEM_MANAGER_EVENT_CAPABILITIES	0U

/* The capabilities' bits. */
#define KL_SYSTEM_CAPABILITY_SETTINGS		0x1U
#define KL_SYSTEM_CAPABILITY_NETWORK		0x2U
#define KL_SYSTEM_CAPABILITY_AUDIO		0x4U
#define KL_SYSTEM_CAPABILITY_POWER		0x8U
#define KL_SYSTEM_CAPABILITY_DEVICES		0x10U
#define KL_SYSTEM_CAPABILITY_MONITOR		0x20U
#define KL_SYSTEM_CAPABILITY_ACCOUNT		0x40U
#define KL_SYSTEM_CAPABILITY_SHARING		0x80U
#define KL_SYSTEM_CAPABILITY_ADMINISTER		0x100U
#define KL_SYSTEM_CAPABILITY_PIN		0x200U
#define KL_SYSTEM_CAPABILITY_NOTIFY		0x400U

/* Since when the manager has get_sharing (ws089-p025), and the account administer and refused (ws089-p026). */
#define KL_SYSTEM_SINCE_SHARING			7U
#define KL_SYSTEM_SINCE_ADMINISTER		8U

/* Since when the account has set_pin (ws163-p003), and enrolled (ws172-p002). */
#define KL_SYSTEM_SINCE_PIN			10U
#define KL_SYSTEM_SINCE_ENROLLED		11U

/* Since when the manager has get_notify (ws156-p002). */
#define KL_SYSTEM_SINCE_NOTIFY			13U

/* Since when the account has add_key, remove_key, key and touch (ws172-p003). */
#define KL_SYSTEM_SINCE_KEYS			14U

/* The interfaces' names (WS131 p010). */
#define KL_SYSTEM_NETWORK_NAME			"kl_system_network_v1"
#define KL_SYSTEM_AUDIO_NAME			"kl_system_audio_v1"
#define KL_SYSTEM_POWER_NAME			"kl_system_power_v1"
#define KL_SYSTEM_DEVICES_NAME			"kl_system_devices_v1"
#define KL_SYSTEM_MONITOR_NAME			"kl_system_monitor_v1"
#define KL_SYSTEM_ACCOUNT_NAME			"kl_system_account_v1"
#define KL_SYSTEM_SHARING_NAME			"kl_system_sharing_v1"
#define KL_SYSTEM_NOTIFY_NAME			"kl_system_notify_v1"

/*
 * kl_system_notify_v1's requests and events (ws156-p002,
 * plan/ws156/phase001/phase.md section 2): post(request, replaces, app,
 * title, body, flags), withdraw(request, id); posted(request, id),
 * activated(id), closed(id, reason), result(request, applied, saved).
 */
#define KL_SYSTEM_NOTIFY_DESTROY		0U
#define KL_SYSTEM_NOTIFY_POST			1U
#define KL_SYSTEM_NOTIFY_WITHDRAW		2U
#define KL_SYSTEM_NOTIFY_EVENT_POSTED		0U
#define KL_SYSTEM_NOTIFY_EVENT_ACTIVATED	1U
#define KL_SYSTEM_NOTIFY_EVENT_CLOSED		2U
#define KL_SYSTEM_NOTIFY_EVENT_RESULT		3U

/* A notification's flags, and why one closed (closed's reason). */
#define KL_SYSTEM_NOTIFY_URGENT			0x1U
#define KL_SYSTEM_NOTIFY_ACTION			0x2U
#define KL_SYSTEM_NOTIFY_DISMISSED		1U
#define KL_SYSTEM_NOTIFY_EXPIRED		2U
#define KL_SYSTEM_NOTIFY_CLEARED		3U
#define KL_SYSTEM_NOTIFY_WITHDRAWN		4U

/* kl_system_sharing_v1's requests and events (ws089-p025), and the longest fingerprint it carries. */
#define KL_SYSTEM_SHARING_DESTROY		0U
#define KL_SYSTEM_SHARING_SET_SSH		1U
#define KL_SYSTEM_SHARING_QUERY			2U
#define KL_SYSTEM_SHARING_EVENT_STATE		0U
#define KL_SYSTEM_SHARING_EVENT_DONE		1U
#define KL_SYSTEM_SHARING_EVENT_RESULT		2U

/* kl_system_account_v1's requests and event, and the longest password it carries (without its NUL). */
#define KL_SYSTEM_ACCOUNT_DESTROY		0U
#define KL_SYSTEM_ACCOUNT_SET_PASSWORD		1U
#define KL_SYSTEM_ACCOUNT_ADMINISTER		2U
#define KL_SYSTEM_ACCOUNT_SET_PIN		3U
#define KL_SYSTEM_ACCOUNT_ADD_KEY		4U
#define KL_SYSTEM_ACCOUNT_REMOVE_KEY		5U
#define KL_SYSTEM_ACCOUNT_EVENT_RESULT		0U
#define KL_SYSTEM_ACCOUNT_EVENT_REFUSED		1U
#define KL_SYSTEM_ACCOUNT_EVENT_ENROLLED	2U
#define KL_SYSTEM_ACCOUNT_EVENT_KEY		3U
#define KL_SYSTEM_ACCOUNT_EVENT_TOUCH		4U
#define KL_SYSTEM_KEY_LABEL_MAX			32U
#define KL_SYSTEM_KEY_REF_MAX			16U
#define KL_SYSTEM_PASSWORD_MAX			256U

/* The longest operation administer carries, and the longest refusal's word (without their NULs). */
#define KL_SYSTEM_OPERATION_MAX			1024U
#define KL_SYSTEM_REASON_MAX			31U

/* kl_system_network_v1's requests and events. */
#define KL_SYSTEM_NETWORK_DESTROY		0U
#define KL_SYSTEM_NETWORK_REQUEST		1U
#define KL_SYSTEM_NETWORK_SAVE_KEY		2U
#define KL_SYSTEM_NETWORK_QUERY_DETAILS		3U
#define KL_SYSTEM_NETWORK_SET_SCANNING		4U
#define KL_SYSTEM_NETWORK_CONFIGURE_WIRED	5U
#define KL_SYSTEM_NETWORK_EVENT_STATE		0U
#define KL_SYSTEM_NETWORK_EVENT_ACCESS_POINT	1U
#define KL_SYSTEM_NETWORK_EVENT_SCAN_DONE	2U
#define KL_SYSTEM_NETWORK_EVENT_LINK		3U
#define KL_SYSTEM_NETWORK_EVENT_DNS		4U
#define KL_SYSTEM_NETWORK_EVENT_SAVED		5U
#define KL_SYSTEM_NETWORK_EVENT_DETAILS_DONE	6U
#define KL_SYSTEM_NETWORK_EVENT_DONE		7U
#define KL_SYSTEM_NETWORK_EVENT_RESULT		8U
#define KL_SYSTEM_NETWORK_EVENT_WIRED		9U
#define KL_SYSTEM_NETWORK_EVENT_LINK_SPEED	10U

/* Since when the network has configure_wired and wired (ws089-p022), and link_speed (BUG-222). */
#define KL_SYSTEM_NETWORK_SINCE_WIRED		6U
#define KL_SYSTEM_NETWORK_SINCE_LINK_SPEED	12U

/* How a wired interface is configured (configure_wired's mode and wired's). */
#define KL_SYSTEM_WIRED_UNKNOWN			0U
#define KL_SYSTEM_WIRED_DHCP			1U
#define KL_SYSTEM_WIRED_STATIC			2U

/* The network's requests (request's what; the backend's KL_BACKEND_NETWORK_REQUEST_* values). */
#define KL_SYSTEM_NETWORK_SCAN			1U
#define KL_SYSTEM_NETWORK_JOIN			2U
#define KL_SYSTEM_NETWORK_DISCONNECT		3U
#define KL_SYSTEM_NETWORK_WIFI_ON		4U
#define KL_SYSTEM_NETWORK_WIFI_OFF		5U

/* A link's flags. */
#define KL_SYSTEM_LINK_UP			0x1U
#define KL_SYSTEM_LINK_RUNNING			0x2U
#define KL_SYSTEM_LINK_LOOPBACK			0x4U

/* kl_system_audio_v1's requests and events. */
#define KL_SYSTEM_AUDIO_DESTROY			0U
#define KL_SYSTEM_AUDIO_SET_VOLUME		1U
#define KL_SYSTEM_AUDIO_FEEDBACK		2U
#define KL_SYSTEM_AUDIO_EVENT_STATE		0U
#define KL_SYSTEM_AUDIO_EVENT_DONE		1U
#define KL_SYSTEM_AUDIO_EVENT_RESULT		2U

/* kl_system_power_v1's requests and events, and its actions (the backend's KL_BACKEND_POWER_* values). */
#define KL_SYSTEM_POWER_DESTROY			0U
#define KL_SYSTEM_POWER_ACTION			1U
#define KL_SYSTEM_POWER_EVENT_STATE		0U
#define KL_SYSTEM_POWER_EVENT_DONE		1U
#define KL_SYSTEM_POWER_EVENT_RESULT		2U
#define KL_SYSTEM_POWER_POWEROFF		1U
#define KL_SYSTEM_POWER_REBOOT			2U
#define KL_SYSTEM_POWER_SUSPEND			3U

/* kl_system_devices_v1's requests and events. */
#define KL_SYSTEM_DEVICES_DESTROY		0U
#define KL_SYSTEM_DEVICES_EJECT			1U
#define KL_SYSTEM_DEVICES_MOUNT			2U
#define KL_SYSTEM_DEVICES_EVENT_DEVICE		0U
#define KL_SYSTEM_DEVICES_EVENT_DONE		1U
#define KL_SYSTEM_DEVICES_EVENT_RESULT		2U
#define KL_SYSTEM_DEVICES_EVENT_BUSY		3U
#define KL_SYSTEM_DEVICES_EVENT_VOLUME		4U
#define KL_SYSTEM_DEVICES_SINCE_MOUNT		5U
#define KL_SYSTEM_DEVICES_SINCE_VOLUME		9U

/* A device's kind and its state's bits (ws132-p004). */
#define KL_SYSTEM_DEVICE_KIND_STORAGE		1U
#define KL_SYSTEM_DEVICE_MOUNTED		0x1U
#define KL_SYSTEM_DEVICE_NEW			0x2U

/* kl_system_monitor_v1's requests and events (WS134 p012). */
#define KL_SYSTEM_MONITOR_DESTROY		0U
#define KL_SYSTEM_MONITOR_ACK			1U
#define KL_SYSTEM_MONITOR_SET_PERIOD		2U
#define KL_SYSTEM_MONITOR_EVENT_INFO		0U
#define KL_SYSTEM_MONITOR_EVENT_DEVICE		1U
#define KL_SYSTEM_MONITOR_EVENT_INFO_DONE	2U
#define KL_SYSTEM_MONITOR_EVENT_CPU		3U
#define KL_SYSTEM_MONITOR_EVENT_MEMORY		4U
#define KL_SYSTEM_MONITOR_EVENT_LINK		5U
#define KL_SYSTEM_MONITOR_EVENT_DISK		6U
#define KL_SYSTEM_MONITOR_EVENT_GPU		7U
#define KL_SYSTEM_MONITOR_EVENT_SAMPLE_DONE	8U

/* A monitor device's kind (the device event's kind). */
#define KL_SYSTEM_MONITOR_DEVICE_GPU		1U
#define KL_SYSTEM_MONITOR_DEVICE_DISK		2U
#define KL_SYSTEM_MONITOR_DEVICE_LINK		3U

/* The bounds of a monitor's period, in milliseconds, and the period without one. */
#define KL_SYSTEM_MONITOR_PERIOD_MIN		250U
#define KL_SYSTEM_MONITOR_PERIOD_MAX		10000U
#define KL_SYSTEM_MONITOR_PERIOD_DEFAULT	1000U

/* kl_system_settings_v1's requests and events. */
#define KL_SYSTEM_SETTINGS_DESTROY		0U
#define KL_SYSTEM_SETTINGS_SET			1U
#define KL_SYSTEM_SETTINGS_RESET		2U
#define KL_SYSTEM_SETTINGS_EVENT_VALUE		0U
#define KL_SYSTEM_SETTINGS_EVENT_DONE		1U
#define KL_SYSTEM_SETTINGS_EVENT_RESULT		2U

/* A value's flags: the resolver's default (not chosen), and not known yet (the value is empty). */
#define KL_SYSTEM_SETTINGS_DEFAULT		0x1U
#define KL_SYSTEM_SETTINGS_UNKNOWN		0x2U

/* The results of a request (WS131 section 4.1). */
#define KL_SYSTEM_RESULT_OK			0U
#define KL_SYSTEM_RESULT_DENIED			1U
#define KL_SYSTEM_RESULT_UNSUPPORTED		2U
#define KL_SYSTEM_RESULT_BUSY			3U
#define KL_SYSTEM_RESULT_INVALID		4U
#define KL_SYSTEM_RESULT_UNAVAILABLE		5U
#define KL_SYSTEM_RESULT_FAILED			6U
#define KL_SYSTEM_RESULT_NOT_SAVED		7U

/* A network join's own failures (WS131 p011): no key is saved, the network refused the key, the network is out of reach. */
#define KL_SYSTEM_RESULT_NO_KEY			8U
#define KL_SYSTEM_RESULT_REFUSED		9U
#define KL_SYSTEM_RESULT_UNREACHABLE		10U

#endif
