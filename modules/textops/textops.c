/*$Id$
 *
 * Copyright (C) 2001-2003 FhG Fokus
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * History:
 * -------
 *  2003-02-28  scratchpad compatibility abandoned (jiri)
 *  2003-01-29: - rewriting actions (replace, search_append) now begin
 *                at the second line -- previously, they could affect
 *                first line too, which resulted in wrong calculation of
 *                forwarded requests and an error consequently
 *              - replace_all introduced
 *  2003-01-28  scratchpad removed (jiri)
 *  2003-01-18  append_urihf introduced (jiri)
 *  2003-03-10  module export interface updated to the new format (andrei)
 *  2003-03-16  flags export parameter added (janakj)
 *  2003-03-19  replaced all mallocs/frees w/ pkg_malloc/pkg_free (andrei)
 *  2003-04-97  actions permitted to be used from failure/reply routes (jiri)
 *  2003-04-21  remove_hf and is_present_hf introduced (jiri)
 *  2003-08-19  subst added (support for sed like res:s/re/repl/flags) (andrei)
 *  2003-08-20  subst_uri added (like above for uris) (andrei)
 *  2003-09-11  updated to new build_lump_rpl() interface (bogdan)
 *  2003-11-11: build_lump_rpl() removed, add_lump_rpl() has flags (bogdan)
 *  2004-05-09: append_time introduced (jiri)
 *  2004-07-06  subst_user added (like subst_uri but only for user) (sobomax)
 *  2004-11-12  subst_user changes (old serdev mails) (andrei)
 *  2005-07-05  is_method("name") to check method using id (ramona)
 *  2006-03-17  applied patch from Marc Haisenko <haisenko@comdasys.com> 
 *              for adding has_body() function (bogdan)
 *
 */


#include "../../action.h"
#include "../../sr_module.h"
#include "../../dprint.h"
#include "../../data_lump.h"
#include "../../data_lump_rpl.h"
#include "../../error.h"
#include "../../mem/mem.h"
#include "../../str.h"
#include "../../re.h"
#include "../../parser/parse_uri.h"
#include "../../parser/parse_hname2.h"
#include "../../parser/parse_methods.h"
#include "../../parser/parse_content.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> /* for regex */
#include <regex.h>
#include <time.h>
#include <sys/time.h>

MODULE_VERSION


/* RFC822-conforming dates format:

   %a -- abbreviated week of day name (locale), %d day of month
   as decimal number, %b abbreviated month name (locale), %Y
   year with century, %T time in 24h notation
*/
#define TIME_FORMAT "Date: %a, %d %b %Y %H:%M:%S GMT"
#define MAX_TIME 64


static int search_f(struct sip_msg*, char*, char*);
static int search_body_f(struct sip_msg*, char*, char*);
static int replace_f(struct sip_msg*, char*, char*);
static int replace_body_f(struct sip_msg*, char*, char*);
static int replace_all_f(struct sip_msg*, char*, char*);
static int replace_body_all_f(struct sip_msg*, char*, char*);
static int subst_f(struct sip_msg*, char*, char*);
static int subst_uri_f(struct sip_msg*, char*, char*);
static int subst_user_f(struct sip_msg*, char*, char*);
static int subst_body_f(struct sip_msg*, char*, char*);
static int remove_hf_f(struct sip_msg* msg, char* str_hf, char* foo);
static int is_present_hf_f(struct sip_msg* msg, char* str_hf, char* foo);
static int search_append_f(struct sip_msg*, char*, char*);
static int search_append_body_f(struct sip_msg*, char*, char*);
static int append_to_reply_f(struct sip_msg* msg, char* key, char* str);
static int append_hf_1(struct sip_msg* msg, char* str1, char* str2);
static int append_hf_2(struct sip_msg* msg, char* str1, char* str2);
static int insert_hf_1(struct sip_msg* msg, char* str1, char* str2);
static int insert_hf_2(struct sip_msg* msg, char* str1, char* str2);
static int append_urihf(struct sip_msg* msg, char* str1, char* str2);
static int append_time_f(struct sip_msg* msg, char* , char *);
static int is_method_f(struct sip_msg* msg, char* , char *);
static int has_body_f(struct sip_msg *msg, char *type, char *str2 );

static int fixup_regex(void**, int);
static int fixup_substre(void**, int);
static int str_fixup(void** param, int param_no);
static int hname_fixup(void** param, int param_no);
static int fixup_method(void** param, int param_no);
static int add_header_fixup(void** param, int param_no);
static int it_list_fixup(void** param, int param_no);
static int fixup_body_type(void** param, int param_no);

static int mod_init(void);


static cmd_export_t cmds[]={
	{"search",           search_f,          1, fixup_regex, 
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"search_body",      search_body_f,     1, fixup_regex, 
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"search_append",    search_append_f,   2, fixup_regex, 
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"search_append_body", search_append_body_f,   2, fixup_regex, 
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"replace",          replace_f,         2, fixup_regex, 
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"replace_body",     replace_body_f,    2, fixup_regex, 
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"replace_all",      replace_all_f,     2, fixup_regex, 
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"replace_body_all", replace_body_all_f,2, fixup_regex, 
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"append_to_reply",  append_to_reply_f, 1, it_list_fixup,
			REQUEST_ROUTE|BRANCH_ROUTE},
	{"append_hf",        append_hf_1,       1, add_header_fixup,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"append_hf",        append_hf_2,       2, add_header_fixup,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"insert_hf",        insert_hf_1,       1, add_header_fixup,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"insert_hf",        insert_hf_2,       2, add_header_fixup,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"append_urihf",     append_urihf,      2, str_fixup,   
			REQUEST_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"remove_hf",        remove_hf_f,       1, hname_fixup,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"is_present_hf",    is_present_hf_f,   1, hname_fixup,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"subst",            subst_f,           1, fixup_substre,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"subst_uri",        subst_uri_f,       1, fixup_substre,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE}, 
	{"subst_user",       subst_user_f,      1, fixup_substre,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"subst_body",       subst_body_f,      1, fixup_substre,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"append_time",      append_time_f,     0, 0,
			REQUEST_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE },
	{"is_method",        is_method_f,       1, fixup_method,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"has_body",         has_body_f,        0, 0,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{"has_body",         has_body_f,        1, fixup_body_type,
			REQUEST_ROUTE|ONREPLY_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{0,0,0,0,0}
};


struct module_exports exports= {
	"textops",  /* module name*/
	cmds,       /* exported functions */
	0,          /* module parameters */
	0,          /* exported statistics */
	mod_init,   /* module initialization function */
	0,          /* response function */
	0,          /* destroy function */
	0,          /* per-child init function */
};


static int mod_init(void)
{
	LOG(L_INFO, "TextOPS - initializing\n");
	return 0;
}

static char *get_header(struct sip_msg *msg)
{
	return msg->buf+msg->first_line.len;
}



static int search_f(struct sip_msg* msg, char* key, char* str2)
{
	/*we registered only 1 param, so we ignore str2*/
	regmatch_t pmatch;

	if (regexec((regex_t*) key, msg->buf, 1, &pmatch, 0)!=0) return -1;
	return 1;
}


static int search_body_f(struct sip_msg* msg, char* key, char* str2)
{
	str body;
	/*we registered only 1 param, so we ignore str2*/
	regmatch_t pmatch;

	body.s = get_body(msg);
	if (body.s==0) {
		LOG(L_ERR, "ERROR:search_body_f: failed to get the message body\n");
		return -1;
	}
	body.len = msg->len -(int)(body.s-msg->buf);
	if (body.len==0) {
		DBG("ERROR:search_body_f: message body has zero length\n");
		return -1;
	}

	if (regexec((regex_t*) key, body.s, 1, &pmatch, 0)!=0) return -1;
	return 1;
}


static int search_append_f(struct sip_msg* msg, char* key, char* str2)
{
	struct lump* l;
	regmatch_t pmatch;
	char* s;
	int len;
	char *begin;
	int off;

	begin=get_header(msg); /* msg->orig/buf previously .. uri problems */
	off=begin-msg->buf;

	if (regexec((regex_t*) key, begin, 1, &pmatch, 0)!=0) return -1;
	if (pmatch.rm_so!=-1){
		if ((l=anchor_lump(msg, off+pmatch.rm_eo, 0, 0))==0)
			return -1;
		len=strlen(str2);
		s=pkg_malloc(len);
		if (s==0){
			LOG(L_ERR, "ERROR: search_append_f: mem. allocation failure\n");
			return -1;
		}
		memcpy(s, str2, len); 
		if (insert_new_lump_after(l, s, len, 0)==0){
			LOG(L_ERR, "ERROR: could not insert new lump\n");
			pkg_free(s);
			return -1;
		}
		return 1;
	}
	return -1;
}

static int search_append_body_f(struct sip_msg* msg, char* key, char* str2)
{
	struct lump* l;
	regmatch_t pmatch;
	char* s;
	int len;
	int off;
	str body;

	body.s = get_body(msg);
	if (body.s==0) {
		LOG(L_ERR,
				"ERROR:search_append_body_f: failed to get the message body\n");
		return -1;
	}
	body.len = msg->len -(int)(body.s-msg->buf);
	if (body.len==0) {
		DBG("ERROR:search_append_body_f: message body has zero length\n");
		return -1;
	}

	off=body.s-msg->buf;

	if (regexec((regex_t*) key, body.s, 1, &pmatch, 0)!=0) return -1;
	if (pmatch.rm_so!=-1){
		if ((l=anchor_lump(msg, off+pmatch.rm_eo, 0, 0))==0)
			return -1;
		len=strlen(str2);
		s=pkg_malloc(len);
		if (s==0){
			LOG(L_ERR, "ERROR: search_append_f: mem. allocation failure\n");
			return -1;
		}
		memcpy(s, str2, len); 
		if (insert_new_lump_after(l, s, len, 0)==0){
			LOG(L_ERR, "ERROR: could not insert new lump\n");
			pkg_free(s);
			return -1;
		}
		return 1;
	}
	return -1;
}


static int replace_all_f(struct sip_msg* msg, char* key, char* str2)
{
	struct lump* l;
	regmatch_t pmatch;
	char* s;
	int len;
	char* begin;
	int off;
	int ret;
	int eflags;

	begin=get_header(msg); /* msg->orig previously .. uri problems */
	ret=-1; /* pessimist: we will not find any */
	len=strlen(str2);
	eflags=0; /* match ^ at the beginning of the string*/

	while (begin<msg->buf+msg->len 
				&& regexec((regex_t*) key, begin, 1, &pmatch, eflags)==0) {
		off=begin-msg->buf;
		/* change eflags, not to match any more at string start */
		eflags|=REG_NOTBOL;
		if (pmatch.rm_so==-1){
			LOG(L_ERR, "ERROR: replace_all_f: offset unknown\n");
			return -1;
		}
		if ((l=del_lump(msg, pmatch.rm_so+off,
						pmatch.rm_eo-pmatch.rm_so, 0))==0) {
			LOG(L_ERR, "ERROR: replace_all_f: del_lump failed\n");
			return -1;
		}
		s=pkg_malloc(len);
		if (s==0){
			LOG(L_ERR, "ERROR: replace_all_f: mem. allocation failure\n");
			return -1;
		}
		memcpy(s, str2, len); 
		if (insert_new_lump_after(l, s, len, 0)==0){
			LOG(L_ERR, "ERROR: replace_all_f: could not insert new lump\n");
			pkg_free(s);
			return -1;
		}
		/* new cycle */
		begin=begin+pmatch.rm_eo;
		ret=1;
	} /* while found ... */
	return ret;
}

static int replace_body_all_f(struct sip_msg* msg, char* key, char* str2)
{
	struct lump* l;
	regmatch_t pmatch;
	char* s;
	int len;
	char* begin;
	int off;
	int ret;
	int eflags;
	str body;

	body.s = get_body(msg);
	if (body.s==0) {
		LOG(L_ERR,
				"ERROR:replace_body_all_f: failed to get the message body\n");
		return -1;
	}
	body.len = msg->len -(int)(body.s-msg->buf);
	if (body.len==0) {
		DBG("ERROR:replace_body_all_f: message body has zero length\n");
		return -1;
	}

	begin=body.s; /* msg->orig previously .. uri problems */
	ret=-1; /* pessimist: we will not find any */
	len=strlen(str2);
	eflags=0; /* match ^ at the beginning of the string*/

	while (begin<msg->buf+msg->len 
				&& regexec((regex_t*) key, begin, 1, &pmatch, eflags)==0) {
		off=begin-msg->buf;
		/* change eflags, not to match any more at string start */
		eflags|=REG_NOTBOL;
		if (pmatch.rm_so==-1){
			LOG(L_ERR, "ERROR: replace_body_all_f: offset unknown\n");
			return -1;
		}
		if ((l=del_lump(msg, pmatch.rm_so+off,
						pmatch.rm_eo-pmatch.rm_so, 0))==0) {
			LOG(L_ERR, "ERROR: replace_body_all_f: del_lump failed\n");
			return -1;
		}
		s=pkg_malloc(len);
		if (s==0){
			LOG(L_ERR, "ERROR: replace_body_all_f: mem. allocation failure\n");
			return -1;
		}
		memcpy(s, str2, len); 
		if (insert_new_lump_after(l, s, len, 0)==0){
			LOG(L_ERR,
				"ERROR: replace_body_all_f: could not insert new lump\n");
			pkg_free(s);
			return -1;
		}
		/* new cycle */
		begin=begin+pmatch.rm_eo;
		ret=1;
	} /* while found ... */
	return ret;
}

static int replace_f(struct sip_msg* msg, char* key, char* str2)
{
	struct lump* l;
	regmatch_t pmatch;
	char* s;
	int len;
	char* begin;
	int off;

	begin=get_header(msg); /* msg->orig previously .. uri problems */

	if (regexec((regex_t*) key, begin, 1, &pmatch, 0)!=0) return -1;
	off=begin-msg->buf;

	if (pmatch.rm_so!=-1){
		if ((l=del_lump(msg, pmatch.rm_so+off,
						pmatch.rm_eo-pmatch.rm_so, 0))==0)
			return -1;
		len=strlen(str2);
		s=pkg_malloc(len);
		if (s==0){
			LOG(L_ERR, "ERROR: replace_f: mem. allocation failure\n");
			return -1;
		}
		memcpy(s, str2, len); 
		if (insert_new_lump_after(l, s, len, 0)==0){
			LOG(L_ERR, "ERROR: could not insert new lump\n");
			pkg_free(s);
			return -1;
		}
		
		return 1;
	}
	return -1;
}

static int replace_body_f(struct sip_msg* msg, char* key, char* str2)
{
	struct lump* l;
	regmatch_t pmatch;
	char* s;
	int len;
	char* begin;
	int off;
	str body;

	body.s = get_body(msg);
	if (body.s==0) {
		LOG(L_ERR,
				"ERROR:replace_body_f: failed to get the message body\n");
		return -1;
	}
	body.len = msg->len -(int)(body.s-msg->buf);
	if (body.len==0) {
		DBG("ERROR:replace_body_f: message body has zero length\n");
		return -1;
	}

	begin=body.s; /* msg->orig previously .. uri problems */

	if (regexec((regex_t*) key, begin, 1, &pmatch, 0)!=0) return -1;
	off=begin-msg->buf;

	if (pmatch.rm_so!=-1){
		if ((l=del_lump(msg, pmatch.rm_so+off,
						pmatch.rm_eo-pmatch.rm_so, 0))==0)
			return -1;
		len=strlen(str2);
		s=pkg_malloc(len);
		if (s==0){
			LOG(L_ERR, "ERROR: replace_body_f: mem. allocation failure\n");
			return -1;
		}
		memcpy(s, str2, len); 
		if (insert_new_lump_after(l, s, len, 0)==0){
			LOG(L_ERR, "ERROR: replace_body_f: could not insert new lump\n");
			pkg_free(s);
			return -1;
		}
		
		return 1;
	}
	return -1;
}


/* sed-perl style re: s/regular expression/replacement/flags */
static int subst_f(struct sip_msg* msg, char*  subst, char* ignored)
{
	struct lump* l;
	struct replace_lst* lst;
	struct replace_lst* rpl;
	char* begin;
	struct subst_expr* se;
	int off;
	int ret;
	int nmatches;
	
	se=(struct subst_expr*)subst;
	begin=get_header(msg);  /* start after first line to avoid replacing
							   the uri */
	off=begin-msg->buf;
	ret=-1;
	if ((lst=subst_run(se, begin, msg, &nmatches))==0)
		goto error; /* not found */
	for (rpl=lst; rpl; rpl=rpl->next){
		DBG(" %s: subst_f: replacing at offset %d [%.*s] with [%.*s]\n",
				exports.name, rpl->offset+off,
				rpl->size, rpl->offset+off+msg->buf,
				rpl->rpl.len, rpl->rpl.s);
		if ((l=del_lump(msg, rpl->offset+off, rpl->size, 0))==0)
			goto error;
		/* hack to avoid re-copying rpl, possible because both 
		 * replace_lst & lumps use pkg_malloc */
		if (insert_new_lump_after(l, rpl->rpl.s, rpl->rpl.len, 0)==0){
			LOG(L_ERR, "ERROR: %s: subst_f: could not insert new lump\n",
					exports.name);
			goto error;
		}
		/* hack continued: set rpl.s to 0 so that replace_lst_free will
		 * not free it */
		rpl->rpl.s=0;
		rpl->rpl.len=0;
	}
	ret=1;
error:
	DBG("subst_f: lst was %p\n", lst);
	if (lst) replace_lst_free(lst);
	if (nmatches<0)
		LOG(L_ERR, "ERROR: %s: subst_run failed\n", exports.name);
	return ret;
}



/* sed-perl style re: s/regular expression/replacement/flags, like
 *  subst but works on the message uri */
static int subst_uri_f(struct sip_msg* msg, char*  subst, char* ignored)
{
	char* tmp;
	int len;
	char c;
	struct subst_expr* se;
	str* result;
	
	se=(struct subst_expr*)subst;
	if (msg->new_uri.s){
		len=msg->new_uri.len;
		tmp=msg->new_uri.s;
	}else{
		tmp=msg->first_line.u.request.uri.s;
		len	=msg->first_line.u.request.uri.len;
	};
	/* ugly hack: 0 s[len], and restore it afterward
	 * (our re functions require 0 term strings), we can do this
	 * because we always alloc len+1 (new_uri) and for first_line, the
	 * message will always be > uri.len */
	c=tmp[len];
	tmp[len]=0;
	result=subst_str(tmp, msg, se, 0); /* pkg malloc'ed result */
	tmp[len]=c;
	if (result){
		DBG("%s: subst_uri_f: match - old uri= [%.*s], new uri= [%.*s]\n",
				exports.name, len, tmp,
				(result->len)?result->len:0,(result->s)?result->s:"");
		if (msg->new_uri.s) pkg_free(msg->new_uri.s);
		msg->new_uri=*result;
		msg->parsed_uri_ok=0; /* reset "use cached parsed uri" flag */
		pkg_free(result); /* free str* pointer */
		return 1; /* success */
	}
	return -1; /* false, no subst. made */
}
	


/* sed-perl style re: s/regular expression/replacement/flags, like
 *  subst but works on the user part of the uri */
static int subst_user_f(struct sip_msg* msg, char*  subst, char* ignored)
{
	int rval;
	str* result;
	struct subst_expr* se;
	struct action act;
	str user;
	char c;
	int nmatches;

	c=0;
	if (parse_sip_msg_uri(msg)<0){
		return -1; /* error, bad uri */
	}
	if (msg->parsed_uri.user.s==0){
		/* no user in uri */
		user.s="";
		user.len=0;
	}else{
		user=msg->parsed_uri.user;
		c=user.s[user.len];
		user.s[user.len]=0;
	}
	se=(struct subst_expr*)subst;
	result=subst_str(user.s, msg, se, &nmatches);/* pkg malloc'ed result */
	if (c)	user.s[user.len]=c;
	if (result == NULL) {
		if (nmatches<0)
			LOG(L_ERR, "subst_user(): subst_str() failed\n");
		return -1;
	}
	/* result->s[result->len] = '\0';  --subst_str returns 0-term strings */
	memset(&act, 0, sizeof(act)); /* be on the safe side */
	act.type = SET_USER_T;
	act.p1_type = STRING_ST;
	act.p1.string = result->s;
	rval = do_action(&act, msg);
	pkg_free(result);
	return rval;
}


/* sed-perl style re: s/regular expression/replacement/flags */
static int subst_body_f(struct sip_msg* msg, char*  subst, char* ignored)
{
	struct lump* l;
	struct replace_lst* lst;
	struct replace_lst* rpl;
	char* begin;
	struct subst_expr* se;
	int off;
	int ret;
	int nmatches;
	str body;

	body.s = get_body(msg);
	if (body.s==0) {
		LOG(L_ERR,
				"ERROR:subst_body_f: failed to get the message body\n");
		return -1;
	}
	body.len = msg->len -(int)(body.s-msg->buf);
	if (body.len==0) {
		DBG("ERROR:subst_body_f: message body has zero length\n");
		return -1;
	}
	
	se=(struct subst_expr*)subst;
	begin=body.s;
	
	off=begin-msg->buf;
	ret=-1;
	if ((lst=subst_run(se, begin, msg, &nmatches))==0)
		goto error; /* not found */
	for (rpl=lst; rpl; rpl=rpl->next){
		DBG("%s:subst_body_f: replacing at offset %d [%.*s] with [%.*s]\n",
				exports.name, rpl->offset+off,
				rpl->size, rpl->offset+off+msg->buf,
				rpl->rpl.len, rpl->rpl.s);
		if ((l=del_lump(msg, rpl->offset+off, rpl->size, 0))==0)
			goto error;
		/* hack to avoid re-copying rpl, possible because both 
		 * replace_lst & lumps use pkg_malloc */
		if (insert_new_lump_after(l, rpl->rpl.s, rpl->rpl.len, 0)==0){
			LOG(L_ERR, "ERROR:%s: subst_body_f: could not insert new lump\n",
					exports.name);
			goto error;
		}
		/* hack continued: set rpl.s to 0 so that replace_lst_free will
		 * not free it */
		rpl->rpl.s=0;
		rpl->rpl.len=0;
	}
	ret=1;
error:
	DBG("subst_body_f: lst was %p\n", lst);
	if (lst) replace_lst_free(lst);
	if (nmatches<0)
		LOG(L_ERR, "ERROR:%s:subst_body_f: subst_run failed\n", exports.name);
	return ret;
}


static int remove_hf_f(struct sip_msg* msg, char* str_hf, char* foo)
{
	struct hdr_field *hf;
	struct lump* l;
	int cnt;

	cnt=0;
	/* we need to be sure we have seen all HFs */
	parse_headers(msg, HDR_EOH_F, 0);
	for (hf=msg->headers; hf; hf=hf->next) {
		if(((str*)str_hf)->s==NULL)
		{
			if (((str*)str_hf)->len!=hf->type)
				continue;
		} else {
			if (hf->name.len!=((str*)str_hf)->len)
				continue;
			if (strncasecmp(hf->name.s, ((str*)str_hf)->s, hf->name.len)!=0)
				continue;
		}
		l=del_lump(msg, hf->name.s-msg->buf, hf->len, 0);
		if (l==0) {
			LOG(L_ERR, "ERROR: remove_hf_f: no memory\n");
			return -1;
		}
		cnt++;
	}
	return cnt==0 ? -1 : 1;
}

static int is_present_hf_f(struct sip_msg* msg, char* str_hf, char* foo)
{
	struct hdr_field *hf;

	/* we need to be sure we have seen all HFs */
	parse_headers(msg, HDR_EOH_F, 0);
	for (hf=msg->headers; hf; hf=hf->next) {
		if(((str*)str_hf)->s==NULL)
		{
			if (((str*)str_hf)->len!=hf->type)
				continue;
		} else {
			if (hf->name.len!=((str*)str_hf)->len)
				continue;
			if (strncasecmp(hf->name.s,((str*)str_hf)->s,hf->name.len)!=0)
				continue;
		}
		return 1;
	}
	return -1;
}



static int fixup_regex(void** param, int param_no)
{
	regex_t* re;

	DBG("module - fixing %s\n", (char*)(*param));
	if (param_no!=1) return 0;
	if ((re=pkg_malloc(sizeof(regex_t)))==0) return E_OUT_OF_MEM;
	if (regcomp(re, *param, REG_EXTENDED|REG_ICASE|REG_NEWLINE) ){
		pkg_free(re);
		LOG(L_ERR, "ERROR: %s : bad re %s\n", exports.name, (char*)*param);
		return E_BAD_RE;
	}
	/* free string */
	pkg_free(*param);
	/* replace it with the compiled re */
	*param=re;
	return 0;
}



static int fixup_substre(void** param, int param_no)
{
	struct subst_expr* se;
	str subst;

	DBG("%s module -- fixing %s\n", exports.name, (char*)(*param));
	if (param_no!=1) return 0;
	subst.s=*param;
	subst.len=strlen(*param);
	se=subst_parser(&subst);
	if (se==0){
		LOG(L_ERR, "ERROR: %s: bad subst. re %s\n", exports.name, 
				(char*)*param);
		return E_BAD_RE;
	}
	/* don't free string -- needed for specifiers */
	/* pkg_free(*param); */
	/* replace it with the compiled subst. re */
	*param=se;
	return 0;
}


static int append_time_f(struct sip_msg* msg, char* p1, char *p2)
{


	size_t len;
	char time_str[MAX_TIME];
	time_t now;
	struct tm *bd_time;

	now=time(0);

	bd_time=gmtime(&now);
	if (bd_time==NULL) {
		LOG(L_ERR, "ERROR: append_time: gmtime failed\n");
		return -1;
	}

	len=strftime(time_str, MAX_TIME, TIME_FORMAT, bd_time);
	if (len>MAX_TIME-2 || len==0) {
		LOG(L_ERR, "ERROR: append_time: unexpected time length\n");
		return -1;
	}

	time_str[len]='\r';
	time_str[len+1]='\n';


	if (add_lump_rpl(msg, time_str, len+2, LUMP_RPL_HDR)==0)
	{
		LOG(L_ERR, "ERROR: append_time: unable to add lump\n");
		return -1;
	}

	return 1;
}


static int append_to_reply_f(struct sip_msg* msg, char* key, char* str0)
{
	xl_elem_t *model;
	str s0;

	if(key==NULL)
	{
		LOG(L_ERR,"ERROR:append_to_reply: error - bad parameters\n");
		return -1;
	}

	model = (xl_elem_t*)key;
	if (xl_printf_s(msg, model, &s0)<0)
	{
		LOG(L_ERR,"textops:add_hf_helper: error - cannot print the format\n");
		return -1;
	}
 
	if ( add_lump_rpl( msg, s0.s, s0.len, LUMP_RPL_HDR)==0 )
	{
		LOG(L_ERR,"ERROR:append_to_reply : unable to add lump_rl\n");
		return -1;
	}

	return 1;
}


/* add str1 to end of header or str1.r-uri.str2 */

static int add_hf_helper(struct sip_msg* msg, str *str1, str *str2,
		xl_elem_t *model, int mode, str *hfs)
{
	struct lump* anchor;
	struct hdr_field *hf;
	char *s;
	int len;
	str s0;

	if (parse_headers(msg, HDR_EOH_F, 0) == -1) {
		LOG(L_ERR, "textops:add_hf_helper: Error while parsing message\n");
		return -1;
	}
	
	hf = 0;
	if(hfs!=NULL) {
		for (hf=msg->headers; hf; hf=hf->next) {
			if((str*)hfs->s==NULL)
			{
				if (((str*)hfs)->len!=hf->type)
				continue;
			} else {
				if (hf->name.len!=((str*)hfs)->len)
					continue;
				if (strncasecmp(hf->name.s,((str*)hfs)->s,hf->name.len)!=0)
					continue;
			}
			break;
		}
	}

	if(mode == 0) { /* append */
		if(hf==0) { /* after last header */
			anchor = anchor_lump(msg, msg->unparsed - msg->buf, 0, 0);
		} else { /* after hf */
			anchor = anchor_lump(msg, hf->name.s + hf->len - msg->buf, 0, 0);
		}
	} else { /* insert */
		if(hf==0) { /* before first header */
			anchor = anchor_lump(msg, msg->headers->name.s - msg->buf, 0, 0);
		} else { /* before hf */
			anchor = anchor_lump(msg, hf->name.s - msg->buf, 0, 0);
		}
	}

	if(anchor == 0) {
		LOG(L_ERR, "textops:add_hf_helper: Can't get anchor\n");
		return -1;
	}

	if(str1) {
		s0 = *str1;
	} else {
		if(model) {
			if (xl_printf_s(msg, model, &s0)<0)
			{
				LOG(L_ERR,
				"textops:add_hf_helper: error - cannot print the format\n");
				return -1;
			}
		} else {
			s0.len = 0;
			s0.s   = 0;
		}
	}
		
	len=s0.len;
	if (str2) len+= str2->len + REQ_LINE(msg).uri.len;

	s = (char*)pkg_malloc(len);
	if (!s) {
		LOG(L_ERR, "textops:add_hf_helper: No memory left\n");
		return -1;
	}

	memcpy(s, s0.s, s0.len);
	if (str2) {
		memcpy(s+str1->len, REQ_LINE(msg).uri.s, REQ_LINE(msg).uri.len);
		memcpy(s+str1->len+REQ_LINE(msg).uri.len, str2->s, str2->len );
	}

	if (insert_new_lump_before(anchor, s, len, 0) == 0) {
		LOG(L_ERR, "textops:add_hf_helper: Can't insert lump\n");
		pkg_free(s);
		return -1;
	}
	return 1;
}

static int append_hf_1(struct sip_msg *msg, char *str1, char *str2 )
{
	return add_hf_helper(msg, 0, 0, (xl_elem_t*)str1, 0, 0);
}

static int append_hf_2(struct sip_msg *msg, char *str1, char *str2 )
{
	return add_hf_helper(msg, 0, 0, (xl_elem_t*)str1, 0,
			(str*)str2);
}

static int insert_hf_1(struct sip_msg *msg, char *str1, char *str2 )
{
	return add_hf_helper(msg, 0, 0, (xl_elem_t*)str1, 1, 0);
}

static int insert_hf_2(struct sip_msg *msg, char *str1, char *str2 )
{
	return add_hf_helper(msg, 0, 0, (xl_elem_t*)str1, 1, 
			(str*)str2);
}

static int append_urihf(struct sip_msg *msg, char *str1, char *str2)
{
	return add_hf_helper(msg, (str*)str1, (str*)str2, 0, 0, 0);
}

static int is_method_f(struct sip_msg *msg, char *meth, char *str2 )
{
	str *m;

	m = (str*)meth;
	if(msg->first_line.type==SIP_REQUEST)
	{
		if(m->s==0)
			return (msg->first_line.u.request.method_value&m->len)?1:-1;
		else
			return (msg->first_line.u.request.method_value==METHOD_OTHER
					&& msg->first_line.u.request.method.len==m->len
					&& (strncasecmp(msg->first_line.u.request.method.s, m->s,
					m->len)==0))?1:-1;
	}
	if(parse_headers(msg, HDR_CSEQ_F, 0)!=0)
	{
		LOG(L_ERR, "textops:is_method: ERROR - cannot parse cseq header\n");
		return -1; /* should it be 0 ?!?! */
	}
	if(m->s==0)
		return (get_cseq(msg)->method_id&m->len)?1:-1;
	else
		return (get_cseq(msg)->method_id==METHOD_OTHER
				&& get_cseq(msg)->method.len==m->len
				&& (strncasecmp(get_cseq(msg)->method.s, m->s,
				m->len)==0))?1:-1;
}

/*
 * Convert char* parameter to str* parameter
 */
static int str_fixup(void** param, int param_no)
{
	str* s;

	s = (str*)pkg_malloc(sizeof(str));
	if (!s) {
		LOG(L_ERR, "str_fixup(): No memory left\n");
		return E_UNSPEC;
	}

	s->s = (char*)*param;
	s->len = strlen(s->s);
	*param = (void*)s;

	return 0;
}

/*
 * Convert char* header_name to str* parameter
 */
static int hname_fixup(void** param, int param_no)
{
	str* s;
	char c;
	struct hdr_field hdr;

	s = (str*)pkg_malloc(sizeof(str));
	if (!s) {
		LOG(L_ERR, "textops:hname_fixup: No memory left\n");
		return E_UNSPEC;
	}

	s->s = (char*)*param;
	s->len = strlen(s->s);
	if(s->len==0)
	{
		LOG(L_ERR,"textops:hname_fixup: empty header name parameter\n");
		pkg_free(s);
		return E_UNSPEC;
	}
	
	c = s->s[s->len];
	s->s[s->len] = ':';
	s->len++;
	
	if (parse_hname2(s->s, s->s + ((s->len<4)?4:s->len), &hdr)==0)
	{
		LOG(L_ERR,"textops:hname_fixup: error parsing header name\n");
		pkg_free(s);
		return E_UNSPEC;
	}
	
	s->len--;
	s->s[s->len] = c;

	if (hdr.type!=HDR_OTHER_T && hdr.type!=HDR_ERROR_T)
	{
		LOG(L_INFO,"INFO:textops:hname_fixup: using "
				"hdr type (%d) instead of <%.*s>\n",
				hdr.type, s->len, s->s);
		pkg_free(s->s);
		s->s = NULL;
		s->len = hdr.type;
	} else {
		LOG(L_INFO,"INFO:textops:hname_fixup: using "
				"hdr type name <%.*s>\n", s->len, s->s);
	}
	
	*param = (void*)s;
	return 0;
}

/*
 * Convert char* method to str* parameter
 */
static int fixup_method(void** param, int param_no)
{
	str* s;
	char *p;
	int m;
	unsigned int method;
	
	s = (str*)pkg_malloc(sizeof(str));
	if (!s) {
		LOG(L_ERR, "textops:fixup_method: No memory left\n");
		return E_UNSPEC;
	}

	s->s = (char*)*param;
	s->len = strlen(s->s);
	if(s->len==0)
	{
		LOG(L_ERR,"textops:fixup_method: empty method name\n");
		pkg_free(s);
		return E_UNSPEC;
	}
	m=0;
	p=s->s;
	while(*p)
	{
		if(*p=='|')
		{
			*p = ',';
			m=1;
		}
		p++;
	}
	if(parse_methods(s, &method)!=0)
	{
		LOG(L_ERR,"textops:fixup_method: bad method names\n");
		pkg_free(s);
		return E_UNSPEC;
	}

	if(m==1)
	{
		if(method==METHOD_UNDEF || method&METHOD_OTHER)
		{
			LOG(L_ERR,
				"textops:fixup_method: unknown method in list [%.*s/%d]"
				" - must be only defined methods\n",
				s->len, s->s, method);
			return E_UNSPEC;
		}
		DBG("textops:fixup_method: using id for methods [%.*s/%d]\n",
				s->len, s->s, method);
		s->s = 0;
		s->len = method;
	} else {
		if(method!=METHOD_UNDEF && method!=METHOD_OTHER)
		{
			DBG("textops:fixup_method: using id for method [%.*s/%d]\n",
				s->len, s->s, method);
			s->s = 0;
			s->len = method;
		} else
			DBG("textops:fixup_method: name for method [%.*s/%d]\n",
				s->len, s->s, method);
	}

	*param = (void*)s;
	return 0;
}


/*
 * Convert char* parameter to xl_elem parameter
 */
static int it_list_fixup(void** param, int param_no)
{
	xl_elem_t *model;
	if(*param)
	{
		if(xl_parse_format((char*)(*param), &model, XL_DISABLE_COLORS)<0)
		{
			LOG(L_ERR, "ERROR:textops:item_list_fixup: wrong format[%s]\n",
				(char*)(*param));
			return E_UNSPEC;
		}
		*param = (void*)model;
	}
	return 0;
}

static int add_header_fixup(void** param, int param_no)
{
	if(param_no==1)
	{
		return it_list_fixup(param, param_no);
	} else if(param_no==2) {
		return hname_fixup(param, param_no);
	} else {
		LOG(L_ERR,
			"ERROR:textops:add_header_fixup: wrong number of parameters\n");
		return E_UNSPEC;
	}
}


static int fixup_body_type(void** param, int param_no)
{
	char *p;
	char *r;
	unsigned int type;

	if(param_no==1) {
		p = (char*)*param;
		if (p==0 || p[0]==0) {
			type = 0;
		} else {
			r = decode_mime_type( p, p+strlen(p) , &type);
			if (r==0) {
				LOG(L_ERR,"ERROR:textops:fixup_body_type: unsupported "
					"mime <%s>\n",p);
				return E_CFG;
			}
			if ( r!=p+strlen(p) ) {
				LOG(L_ERR,"ERROR:textops:fixup_body_type: multiple mimes not "
					"supported!\n");
				return E_CFG;
			}
		}
		pkg_free(*param);
		*param = (void*)(long)type;
	}
	return 0;

}


static int has_body_f(struct sip_msg *msg, char *type, char *str2 )
{
	int mime;

	/* get body pointer */
	if ( get_body(msg)==0 )
		return -1;

	/* all headears are already parsed by "get_body" */
	if (msg->content_length==0) {
		LOG (L_ERR, "ERROR:textops:has_body: very bogus message with body "
			"but no content length hdr\n");
		return -1;
	}

	if (get_content_length (msg)==0) {
		DBG("DEBUG:textops:has_body: content length is zero\n");
		/* Nothing to see here, please move on. */
		return -1;
	}

	/* check type also? */
	if (type==0)
		return 1;

	mime = parse_content_type_hdr (msg);
	if (mime<0) {
		LOG (L_ERR, "ERROR:textops:has_body: failed to extract "
			"content type hdr\n");
		return -1;
	}
	if (mime==0) {
		/* content type hdr not found -> according the RFC3261 we
		 * assume APPLICATION/SDP  --bogdan */
		mime = ((TYPE_APPLICATION << 16) + SUBTYPE_SDP);
	}
	DBG("DBUG:textops:has_body: Content type is %d\n",mime);

	if ( (unsigned int)mime!=(unsigned int)(unsigned long)type )
		return -1;

	return 1;
}
