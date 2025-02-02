/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef __GNUC__
#include <unistd.h>
#else
#include <io.h>
#endif
#include <time.h>

#include "xchat.h"
#include "notify.h"
#include "cfgfiles.h"
#include "fe.h"
#include "server.h"
#include "text.h"
#include "util.h"
#include "xchatc.h"


GSList *notify_list = 0;
int notify_tag = 0;


static char* despacify_dup(char *str)
{
	char *p, *res = (char*)malloc(strlen(str) + 1);

	p = res;
	while (1)
	{
		if (*str != ' ')
		{
			*p = *str;
			if (*p == 0)
				return res;
			p++;
		}
		str++;
	}
}

static int notify_netcmp(char *str, void *serv)
{
	char *net = despacify_dup(server_get_network((server*)serv, TRUE));

	if (rfc_casecmp(str, net) == 0)
	{
		free(net);
		return 0;	// finish & return FALSE from token_foreach()
	}

	free(net);
	return 1;	// keep going...
}

// monitor this nick on this particular network?

static bool notify_do_network(notify *notify, server *serv)
{
	if (!notify->networks)	// ALL networks for this nick
		return TRUE;

	if (token_foreach(notify->networks, ',', notify_netcmp, serv))
		return FALSE;	// network list doesn't contain this one

	return TRUE;
}

notify_per_server* notify_find_server_entry(notify *notify, server *serv)
{
	GSList *list = notify->server_list;
	notify_per_server *servnot;

	while (list)
	{
		servnot = (notify_per_server*)list->data;
		if (servnot->server == serv)
			return servnot;
		list = list->next;
	}

	/* not found, should we add it, or is this not a network where
	   we're monitoring this nick? */
	if (!notify_do_network(notify, serv))
		return nullptr;

	servnot = (notify_per_server*)malloc(sizeof(notify_per_server));
	if (servnot)
	{
		memset(servnot, 0, sizeof(notify_per_server));
		servnot->server = serv;
		servnot->notify = notify;
		notify->server_list = g_slist_prepend(notify->server_list, servnot);
	}
	return servnot;
}

void notify_save()
{
	int fh;
	notify *notify;
	GSList *list = notify_list;

	fh = xchat_open_file("notify.conf", O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh != -1)
	{
		while (list)
		{
			notify = (struct notify*)list->data;
			write(fh, notify->name, strlen(notify->name));
			if (notify->networks)
			{
				write(fh, " ", 1);
				write(fh, notify->networks, strlen(notify->networks));
			}
			write(fh, "\n", 1);
			list = list->next;
		}
		close(fh);
	}
}

void notify_load()
{
	int fh;
	char buf[256];
	char *sep;

	fh = xchat_open_file("notify.conf", O_RDONLY, 0, 0);
	if (fh != -1)
	{
		while (waitline(fh, buf, sizeof buf, FALSE) != -1)
		{
			if (buf[0] != '#' && buf[0] != 0)
			{
				sep = strchr(buf, ' ');
				if (sep)
				{
					sep[0] = 0;
					notify_adduser(buf, sep + 1);
				}
				else
					notify_adduser(buf, nullptr);
			}
		}
		close(fh);
	}
}

static notify_per_server* notify_find(server *serv, char *nick)
{
	GSList *list = notify_list;
	notify_per_server *servnot;
	notify *notify;

	while (list)
	{
		notify = (struct notify*)list->data;

		servnot = notify_find_server_entry(notify, serv);
		if (!servnot)
		{
			list = list->next;
			continue;
		}

		if (!serv->p_cmp(notify->name, nick))
			return servnot;

		list = list->next;
	}

	return 0;
}

static void notify_announce_offline(server* serv, notify_per_server *servnot, char *nick, int quiet)
{
	session *sess;

	sess = serv->front_session;

	servnot->ison = FALSE;
	servnot->lastoff = time(0);
	if (!quiet)
		EMIT_SIGNAL(XP_TE_NOTIFYOFFLINE, sess, nick, serv->servername, server_get_network(serv, TRUE), nullptr, 0);
	fe_notify_update(nick);
	fe_notify_update(0);
}

static void notify_announce_online(server* serv, notify_per_server *servnot, char *nick)
{
	session *sess;

	sess = serv->front_session;

	servnot->lastseen = time(0);
	if (servnot->ison)
		return;

	servnot->ison = TRUE;
	servnot->laston = time(0);
	EMIT_SIGNAL(XP_TE_NOTIFYONLINE, sess, nick, serv->servername, server_get_network(serv, TRUE), nullptr, 0);
	fe_notify_update(nick);
	fe_notify_update(0);

	if (prefs.whois_on_notifyonline)
	{
		// Let's do whois with idle time (like in /quote WHOIS %s %s)

		char *wii_str = (char*)malloc(strlen(nick) * 2 + 2);
		sprintf(wii_str, "%s %s", nick, nick);
		serv->p_whois(serv, wii_str);
		free(wii_str);
	}
}

// handles numeric 601

void notify_set_offline(server* serv, char *nick, int quiet)
{
	notify_per_server *servnot;

	servnot = notify_find(serv, nick);
	if (!servnot)
		return;

	notify_announce_offline(serv, servnot, nick, quiet);
}

// handles numeric 604 and 600

void notify_set_online(server* serv, char *nick)
{
	notify_per_server *servnot;

	servnot = notify_find(serv, nick);
	if (!servnot)
		return;

	notify_announce_online(serv, servnot, nick);
}

static void notify_watch(server* serv, char *nick, int add)
{
	char tbuf[256];

	snprintf(tbuf, sizeof(tbuf), "WATCH +%s", nick);
	if (!add)
		tbuf[6] = '-';
	serv->p_raw(serv, tbuf);
}

static void notify_watch_all(notify *notify, int add)
{
	server *serv;
	GSList *list = serv_list;
	while (list)
	{
		serv = (server*)list->data;
		if (serv->connected && serv->end_of_motd && serv->supports_watch &&
			notify_do_network(notify, serv))
			notify_watch(serv, notify->name, add);
		list = list->next;
	}
}

static void notify_flush_watches(server* serv, GSList *from, GSList *end)
{
	char tbuf[512];
	GSList *list;
	notify *notify;

	strcpy(tbuf, "WATCH");

	list = from;
	while (list != end)
	{
		notify = (struct notify*)list->data;
		strcat(tbuf, " +");
		strcat(tbuf, notify->name);
		list = list->next;
	}
	serv->p_raw(serv, tbuf);
}

// called when logging in. e.g. when End of motd.

void notify_send_watches(server* serv)
{
	notify *notify;
	GSList *list;
	GSList *point;
	int len;

	len = 0;
	point = list = notify_list;
	while (list)
	{
		notify = (struct notify*)list->data;

		if (notify_do_network(notify, serv))
		{
			len += strlen(notify->name) + 2; // + and space;
			if (len > 500)
			{
				notify_flush_watches(serv, point, list);
				len = strlen(notify->name) + 2;
				point = list;
			}
		}

		list = list->next;
	}

	if (point)
		notify_flush_watches(serv, point, nullptr);
}

// called when receiving a ISON 303 - should this func go?

void notify_markonline(server *serv, char *word[])
{
	notify *notify;
	notify_per_server *servnot;
	GSList *list = notify_list;
	int i, seen;

	while (list)
	{
		notify = (struct notify*)list->data;
		servnot = notify_find_server_entry(notify, serv);
		if (!servnot)
		{
			list = list->next;
			continue;
		}
		i = 4;
		seen = FALSE;
		while (*word[i])
		{
			if (!serv->p_cmp(notify->name, word[i]))
			{
				seen = TRUE;
				notify_announce_online(serv, servnot, notify->name);
				break;
			}
			i++;
			/* FIXME: word[] is only a 32 element array, limits notify list to
			   about 27 people */
			if (i > PDIWORDS - 5)
			{
				//fprintf(stderr, _("*** XCHAT WARNING: notify list too large.\n"));
				break;
			}
		}
		if (!seen && servnot->ison)
		{
			notify_announce_offline(serv, servnot, notify->name, FALSE);
		}
		list = list->next;
	}
	fe_notify_update(0);
}

// yuck! Old routine for ISON notify

static void notify_checklist_for_server(server *serv)
{
	char outbuf[512];
	notify *notify;
	GSList *list = notify_list;
	int i = 0;

	strcpy(outbuf, "ISON ");
	while (list)
	{
		notify = (struct notify*)list->data;
		if (notify_do_network(notify, serv))
		{
			i++;
			strcat(outbuf, notify->name);
			strcat(outbuf, " ");
			if (strlen(outbuf) > 460)
			{
				/* LAME: we can't send more than 512 bytes to the server, but
				 * if we split it in two packets, our offline detection wouldn't
				 * work*/
				//fprintf(stderr, _("*** XCHAT WARNING: notify list too large.\n"));
				break;
			}
		}
		list = list->next;
	}

	if (i)
		serv->p_raw(serv, outbuf);
}

int notify_checklist()	// check ISON list
{
	server *serv;
	GSList *list = serv_list;

	while (list)
	{
		serv = (server*)list->data;
		if (serv->connected && serv->end_of_motd && !serv->supports_watch)
		{
			notify_checklist_for_server(serv);
		}
		list = list->next;
	}
	return 1;
}

void
notify_showlist (session *sess)
{
	char outbuf[256];
	notify *notify;
	GSList *list = notify_list;
	notify_per_server *servnot;
	int i = 0;

	EMIT_SIGNAL (XP_TE_NOTIFYHEAD, sess, nullptr, nullptr, nullptr, nullptr, 0);
	while (list)
	{
		i++;
		notify = (struct notify*)list->data;
		servnot = notify_find_server_entry (notify, sess->server);
		if (servnot && servnot->ison)
			snprintf (outbuf, sizeof (outbuf), _("  %-20s online\n"), notify->name);
		else
			snprintf (outbuf, sizeof (outbuf), _("  %-20s offline\n"), notify->name);
		PrintText (sess, outbuf);
		list = list->next;
	}
	if (i)
	{
		sprintf (outbuf, "%d", i);
		EMIT_SIGNAL (XP_TE_NOTIFYNUMBER, sess, outbuf, nullptr, nullptr, nullptr, 0);
	} else
		EMIT_SIGNAL (XP_TE_NOTIFYEMPTY, sess, nullptr, nullptr, nullptr, nullptr, 0);
}

int
notify_deluser (char *name)
{
	notify *notify;
	notify_per_server *servnot;
	GSList *list = notify_list;

	while (list)
	{
		notify = (struct notify*)list->data;
		if (!rfc_casecmp (notify->name, name))
		{
			fe_notify_update (notify->name);
			/* Remove the records for each server */
			while (notify->server_list)
			{
				servnot = (notify_per_server*)notify->server_list->data;
				notify->server_list =
					g_slist_remove (notify->server_list, servnot);
				free (servnot);
			}
			notify_list = g_slist_remove (notify_list, notify);
			notify_watch_all (notify, FALSE);
			if (notify->networks)
				free (notify->networks);
			free (notify->name);
			free (notify);
			fe_notify_update (0);
			return 1;
		}
		list = list->next;
	}
	return 0;
}

void notify_adduser(char *name, char *networks)
{
	notify *notify = (struct notify*)malloc(sizeof(struct notify));
	if (notify)
	{
		memset(notify, 0, sizeof(struct notify));
		if (strlen(name) >= NICKLEN)
		{
			notify->name = (char*)malloc(NICKLEN);
			safe_strcpy(notify->name, name, NICKLEN);
		}
		else
		{
			notify->name = strdup(name);
		}
		if (networks)
			notify->networks = despacify_dup(networks);
		notify->server_list = 0;
		notify_list = g_slist_prepend(notify_list, notify);
		notify_checklist();
		fe_notify_update(notify->name);
		fe_notify_update(0);
		notify_watch_all(notify, TRUE);
	}
}

bool notify_is_in_list(server *serv, char *name)
{
	notify *notify;
	GSList *list = notify_list;

	while (list)
	{
		notify = (struct notify*)list->data;
		if (!serv->p_cmp (notify->name, name))
			return TRUE;
		list = list->next;
	}

	return FALSE;
}

int
notify_isnotify (session *sess, char *name)
{
	notify *notify;
	notify_per_server *servnot;
	GSList *list = notify_list;

	while (list)
	{
		notify = (struct notify*)list->data;
		if (!sess->server->p_cmp (notify->name, name))
		{
			servnot = notify_find_server_entry (notify, sess->server);
			if (servnot && servnot->ison)
				return TRUE;
		}
		list = list->next;
	}

	return FALSE;
}

void notify_cleanup()
{
	GSList *list = notify_list;
	GSList *nslist, *srvlist;
	notify *notify;
	notify_per_server *servnot;
	server *serv;
	int valid;

	while (list)
	{
		// Traverse the list of notify structures
		notify = (struct notify*)list->data;
		nslist = notify->server_list;
		while (nslist)
		{
			// Look at each per-server structure
			servnot = (notify_per_server*)nslist->data;

			// Check the server is valid
			valid = FALSE;
			srvlist = serv_list;
			while (srvlist)
			{
				serv = (server*)srvlist->data;
				if (servnot->server == serv)
				{
					valid = serv->connected;	// Only valid if server is too
					break;
				}
				srvlist = srvlist->next;
			}
			if (!valid)
			{
				notify->server_list = g_slist_remove(notify->server_list, servnot);
				free(servnot);
				nslist = notify->server_list;
			}
			else
			{
				nslist = nslist->next;
			}
		}
		list = list->next;
	}
	fe_notify_update(0);
}
