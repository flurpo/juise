/*
 * $Id$
 *
 * Copyright (c) 2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * juise -- driver for libjuise, allowing remote access for scripting
 */

#include <err.h>
#include <time.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include <pwd.h>
#include <sys/socket.h>

#include <libxml/tree.h>
#include <libxml/dict.h>
#include <libxslt/transform.h>
#include <libxml/HTMLparser.h>
#include <libxml/xmlsave.h>
#include <libexslt/exslt.h>
#include <libxslt/xsltutils.h>

#if 0
#include <libxml/globals.h>
#endif

#include "config.h"
#include <libslax/slax.h>
#include <libslax/slaxconfig.h>

#include <libjuise/string/strextra.h>
#include <libjuise/time/timestr.h>
#include <libjuise/xml/libxml.h>
#include <libjuise/io/trace.h>
#include <libjuise/xml/jsio.h>
#include <libjuise/xml/extensions.h>
#include <libjuise/xml/juisenames.h>
#include <libjuise/juiseconfig.h>

#include "juise.h"

static int script_count;

static char *juise_dir;

void
srv_set_juise_dir (const char *jdir)
{
    if (juise_dir)
	free(juise_dir);
    juise_dir = strdup(jdir);
}

static lx_document_t *
srv_build_input_doc (lx_document_t *input)
{
    return input;
}

static FILE *
open_script (const char *scriptname, char *full_name, int full_size)
{
    char const *extentions[] = { "slax", "xsl", "xslt", "sh", "pl", "", NULL };
    char const **cpp;
    FILE *fp;
    const char *jdir = juise_dir ?: JUISE_SCRIPT_DIR;

    for (cpp = extentions; *cpp; cpp++) {
	snprintf(full_name, full_size, "%s/%s%s%s",
		 jdir, scriptname, **cpp ? "." : "", *cpp);
	fp = fopen(full_name, "r+");
	if (fp)
	    return fp;
    }

    return NULL;
}

static int
srv_run_script (js_session_t *jsp, const char *scriptname,
		lx_document_t *input)
{
    lx_document_t *scriptdoc;
    FILE *scriptfile;
    lx_document_t *indoc;
    lx_stylesheet_t *script;
    lx_document_t *res = NULL;
    char full_name[MAXPATHLEN];

    script_count += 1;

    if (scriptname == NULL)
	errx(1, "missing script name");

    scriptfile = open_script(scriptname, full_name, sizeof(full_name));
    if (scriptfile == NULL) {
	trace(trace_file, TRACE_ALL,
	      "file open failed for script '%s'", scriptname);
	return TRUE;
    }

    scriptdoc = slaxLoadFile(scriptname, scriptfile, NULL, 0);
    if (scriptdoc == NULL)
	errx(1, "cannot parse: '%s'", scriptname);

    if (scriptfile != stdin)
	fclose(scriptfile);

    script = xsltParseStylesheetDoc(scriptdoc);
    if (script == NULL || script->errors != 0)
	errx(1, "%d errors parsing script: '%s'",
	     script ? script->errors : 1, scriptname);

    indoc = srv_build_input_doc(input);
    if (indoc == NULL)
	errx(1, "unable to build input document");

    if (opt_indent)
	script->indent = 1;

    if (opt_debugger) {
	slaxDebugInit();
	slaxDebugSetStylesheet(script);
	slaxDebugApplyStylesheet(scriptname, script, "input",
				 indoc, NULL);
    } else {
	res = xsltApplyStylesheet(script, indoc, NULL);

	xsltSaveResultToFile(jsp->js_fpout, res, script);

	if (res)
	    xmlFreeDoc(res);
    }

    if (indoc != input)
	xmlFreeDoc(indoc);
    xsltFreeStylesheet(script);

    return FALSE;
}

void
run_server (int fdin, int fdout, session_type_t stype)
{
    static const char rpc_reply_open[] = "<rpc-reply>\n";
    static const char rpc_reply_close[] = "</rpc-reply>]]>]]>\n";
    static const char rpc_error[]
	= "<error><message>invalid rpc</message></error>";
    js_session_t *jsp;
    lx_document_t *rpc;
    const char *name;
				 
    jsp = js_session_open_server(fdin, fdout, stype, 0);
    if (jsp == NULL)
	errx(1, "could not open server");

    for (;;) {
	rpc = js_rpc_get_request(jsp);
	if (rpc == NULL) {
	    if (jsp->js_state == JSS_DEAD || jsp->js_state == JSS_CLOSE)
		break;
	    continue;
	}

	name = js_rpc_get_name(rpc);
	if (name) {
	    if (write(fdout, rpc_reply_open, sizeof(rpc_reply_open) - 1) < 0)
		trace(trace_file, TRACE_ALL, "error writing reply: %m");

	    if (srv_run_script(jsp, name, rpc)) {
		(void) write(fdout, rpc_error, sizeof(rpc_error) - 1);
	    }
	    if (write(fdout, rpc_reply_close, sizeof(rpc_reply_close) - 1) < 0)
		trace(trace_file, TRACE_ALL, "error writing reply: %m");
	}

	js_rpc_free(rpc);
    }

    js_session_terminate(jsp);
}
