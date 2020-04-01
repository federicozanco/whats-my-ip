/*
 * What's My IP Plugin
 *  Copyright (C) 2013
 *      Riccardo Catto <karnogh (at) gmail.com>
 *      Federico Zanco <federico.zanco ( at ) gmail.com>
 *
 *
 * License:
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02111-1301, USA.
 *
 */
 
#define PLUGIN_ID			"whats-my-ip"
#define PLUGIN_NAME			"What's my IP"
#define PLUGIN_VERSION		"0.0.1"
#define PLUGIN_STATIC_NAME	"whats-my-ip"
#define PLUGIN_SUMMARY		"Fetches external IP address"
#define PLUGIN_DESCRIPTION	"Fetches external IP address (via web) of the machine where the plugin is running"
#define PLUGIN_AUTHOR		"Riccardo Catto <karnogh@gmail.com>, Federico Zanco <federico.zanco@gmail.com>"
#define PLUGIN_WEBSITE		"http://www.siorarina.net/whats-my-ip/"

#define PREF_PREFIX					    "/plugins/core/" PLUGIN_ID
#define PREF_WHATS_MY_IP_URL	        PREF_PREFIX "url"
#define PREF_WHATS_MY_IP_URL_DEFAULT    "http://checkip.dyndns.org"
#define PREF_REGEX                      PREF_PREFIX "regex"
#define PREF_REGEX_DEFAULT              "Current IP Address: \\b(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\b"

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* config.h may define PURPLE_PLUGINS; protect the definition here so that we
 * don't get complaints about redefinition when it's not necessary. */
#ifndef PURPLE_PLUGINS
# define PURPLE_PLUGINS
#endif

#ifdef GLIB_H
# include <glib.h>
#endif

/* This will prevent compiler errors in some instances and is better explained in the
 * how-to documents on the wiki */
#ifndef G_GNUC_NULL_TERMINATED
# if __GNUC__ >= 4
#  define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
# else
#  define G_GNUC_NULL_TERMINATED
# endif
#endif

#include <debug.h>
#include <plugin.h>
#include <account.h>
#include <conversation.h>
#include <version.h>
#include <prefs.h>
#include <string.h>

typedef enum
{
    IDLE = 0,
        WAITING_CONV,
        FETCHING_URL,
}req_state;

static PurplePlugin	*this_plugin = NULL;
static char *senderp = NULL;
static req_state state = IDLE;

static gchar *
get_ip_addr(const gchar *input_string)
{
    GError *error = NULL;
    GRegex *regex;
    GMatchInfo *match_info;
    
    gchar *match = NULL;
      
    regex = g_regex_new(purple_prefs_get_string(PREF_REGEX), 0, 0, &error);
    
    if (error != NULL)
    {
        purple_debug_error(PLUGIN_STATIC_NAME, "%s\n", error->message );
        g_clear_error(&error);
    }
     
    g_regex_match(regex, input_string, 0, &match_info);
     
    purple_debug_info(
        PLUGIN_STATIC_NAME,
        "searching for a match \"%s\" in the string:\n\"%s\"\n\n", 
        purple_prefs_get_string(PREF_REGEX),
        input_string);

    if (g_match_info_matches(match_info))
    {
        match = g_match_info_fetch(match_info, 0);
         
        purple_debug_info(PLUGIN_STATIC_NAME, "found match: \"%s\"\n", match);
        g_match_info_next(match_info, &error);
     
        if(error != NULL)
        {
            purple_debug_error(PLUGIN_STATIC_NAME, "%s\n", error->message );
            g_clear_error(&error);
        }
    }

    g_match_info_free(match_info);
    g_regex_unref(regex);
    
    return match;
}

static char *
strip_resource(char *sender)
{
    gchar **namev = NULL;
    char *name = NULL;

    namev = g_strsplit(sender, "/", 2);
    
    name = g_strdup(namev[0]);
    g_strfreev(namev);
    
    return name;
}


static void
fetch_url_cb(PurpleUtilFetchUrlData *url_data, gpointer conv, 
             const gchar *url_text, gsize len, const gchar *error_message)
{       
    gchar *match = NULL;
    purple_debug_info(PLUGIN_STATIC_NAME, "fetch_url_cb\n");
    // purple_debug_info(PLUGIN_STATIC_NAME, "%s\n", url_text);
    
    
    if (error_message)
    {
        purple_conv_im_send(PURPLE_CONV_IM(conv), error_message);
        state = IDLE;
        
        return;
    }
        
    /*
    // this works only with http://checkip.dyndns.org url
    purple_conv_im_send(
        PURPLE_CONV_IM(conv),
        purple_markup_strip_html(
            g_strstr_len(url_text, len, "<body>")));
            */
        
    match = get_ip_addr(url_text);
        
    if (match)
    {
        purple_conv_im_send(PURPLE_CONV_IM(conv), match);
        g_free(match);
    }
    
    state = IDLE;
}


static void
conversation_created_cb(PurpleConversation *conv, gpointer data)
{
    purple_debug_info(PLUGIN_STATIC_NAME, "conversation_created_cb");
    
    if (conv && senderp && g_str_has_prefix(senderp, purple_conversation_get_name(conv)) == 0)
    {
        state = FETCHING_URL;
        purple_util_fetch_url(
                purple_prefs_get_string(PREF_WHATS_MY_IP_URL),
                TRUE,
                NULL,
                FALSE,
                fetch_url_cb,
                (gpointer ) conv);
        
        g_free(senderp);
        senderp = NULL;
        
        purple_signal_disconnect(
            purple_conversations_get_handle(),
            "conversation-created",
            this_plugin,
            PURPLE_CALLBACK(conversation_created_cb));
    }
}


static void
received_im_msg_cb(PurpleAccount *account, char *sender, char *buffer,
                   PurpleConversation *conv, PurpleMessageFlags flags, void *data)
{
    purple_debug_info(PLUGIN_STATIC_NAME, "received_im_msg_cb\n");
    
    if (!g_strcmp0(buffer,"ip") && state == IDLE)
    {
        if (conv)
        {
            state = FETCHING_URL;
            purple_util_fetch_url(
                purple_prefs_get_string(PREF_WHATS_MY_IP_URL),
                TRUE,
                NULL,
                FALSE,
                fetch_url_cb,
                (gpointer ) conv);
        } else {
            senderp = strip_resource(sender);
            state = WAITING_CONV;
            purple_signal_connect(purple_conversations_get_handle(),
                "conversation-created",
                this_plugin,
                PURPLE_CALLBACK(conversation_created_cb),
                NULL);
        }
    }
}


static gboolean
plugin_load (PurplePlugin *plugin)
{
	this_plugin = plugin;

	purple_debug_info(PLUGIN_STATIC_NAME, "plugin_load\n");
   
    // received-im-msg
    purple_signal_connect(purple_conversations_get_handle(),
        "received-im-msg",
        plugin,
        PURPLE_CALLBACK(received_im_msg_cb),
        NULL);
    
	return TRUE;
}


static PurplePluginPrefFrame *
get_plugin_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *pref;

	frame = purple_plugin_pref_frame_new();

	pref = purple_plugin_pref_new_with_name_and_label(
				PREF_WHATS_MY_IP_URL,
				"Url where to fetch external IP address from");
	purple_plugin_pref_frame_add(frame, pref);

    pref = purple_plugin_pref_new_with_name_and_label(
                PREF_REGEX,
                "Regex (PCRE) used to get IP address from html");
    purple_plugin_pref_frame_add(frame, pref);
    
	return frame;
}


static PurplePluginUiInfo prefs_info = {
	get_plugin_pref_frame,
	0,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};


/* For specific notes on the meanings of each of these members, consult the C Plugin Howto
 * on the website. */
static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,
	PLUGIN_ID,
	PLUGIN_NAME,
	PLUGIN_VERSION,
	PLUGIN_SUMMARY,
	PLUGIN_DESCRIPTION,
	PLUGIN_AUTHOR,
	PLUGIN_WEBSITE,
	plugin_load,
	NULL,
	NULL,
	NULL,
	NULL,
	&prefs_info,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};


static void
init_plugin (PurplePlugin * plugin)
{
	// add plugin pref
	purple_prefs_add_none(PREF_PREFIX);

	// check url pref
	if (!purple_prefs_exists(PREF_WHATS_MY_IP_URL))
		purple_prefs_add_string(PREF_WHATS_MY_IP_URL, PREF_WHATS_MY_IP_URL_DEFAULT);
    
    // check regex pref
    if (!purple_prefs_exists(PREF_REGEX))
        purple_prefs_add_string(PREF_REGEX, PREF_REGEX_DEFAULT);
}


PURPLE_INIT_PLUGIN (PLUGIN_ID, init_plugin, info)
