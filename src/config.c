#include <sys/stat.h>

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#include "server.h"
#include "log.h"
#include "stream.h"
#include "plugin.h"
#ifdef USE_LICENSE
#include "license.h"
#endif

#include "configparser.h"
#include "configfile.h"

/* handle global options */

int config_insert_values_global(server *srv, array *ca, const config_values_t cv[]) {
	size_t i;
	data_unset *du;
	
	for (i = 0; cv[i].key; i++) {
		data_string *touched;
		
		if (NULL == (du = array_get_element(ca, cv[i].key))) {
			/* no found */
			
			continue;
		}
		
		/* touched */
		touched = data_string_init();
		
		buffer_copy_string(touched->value, "");
		buffer_copy_string_buffer(touched->key, du->key);
		
		array_insert_unique(srv->config_touched, (data_unset *)touched);
	}
	
	return config_insert_values_internal(srv, ca, cv);
}

/* parse config array */

int config_insert_values_internal(server *srv, array *ca, const config_values_t cv[]) {
	size_t i;
	data_unset *du;
	
	for (i = 0; cv[i].key; i++) {
		
		if (NULL == (du = array_get_element(ca, cv[i].key))) {
			/* no found */
			
			continue;
		}
		
		switch (cv[i].type) {
		case T_CONFIG_ARRAY:
			if (du->type == TYPE_ARRAY) {
				size_t j;
				data_array *da = (data_array *)du;
				
				for (j = 0; j < da->value->used; j++) {
					if (da->value->data[j]->type == TYPE_STRING) {
						data_string *ds = data_string_init();
						
						buffer_copy_string_buffer(ds->value, ((data_string *)(da->value->data[j]))->value);
						buffer_copy_string_buffer(ds->key, ((data_string *)(da->value->data[j]))->key);
						
						array_insert_unique(cv[i].destination, (data_unset *)ds);
					} else {
						log_error_write(srv, __FILE__, __LINE__, "sssbs", "unexpected type for key: ", cv[i].key, "[", da->value->data[i]->key, "](string)");
						
						return -1;
					}
				}
			} else {
				log_error_write(srv, __FILE__, __LINE__, "sss", "unexpected type for key: ", cv[i].key, "array of strings");
				
				return -1;
			}
			break;
		case T_CONFIG_STRING:
			if (du->type == TYPE_STRING) {
				data_string *ds = (data_string *)du;
				
				buffer_copy_string_buffer(cv[i].destination, ds->value);
			} else {
				log_error_write(srv, __FILE__, __LINE__, "ssss", "unexpected type for key: ", cv[i].key, "(string)", "\"...\"");
				
				return -1;
			}
			break;
		case T_CONFIG_SHORT:
			switch(du->type) {
			case TYPE_INTEGER: {
				data_integer *di = (data_integer *)du;
				
				*((unsigned short *)(cv[i].destination)) = di->value;
				break;
			}
			case TYPE_STRING: {
				data_string *ds = (data_string *)du;
					
				log_error_write(srv, __FILE__, __LINE__, "ssbss", "unexpected type for key: ", cv[i].key, ds->value, "(short)", "0 ... 65535");
				
				return -1;
			}
			default:
				log_error_write(srv, __FILE__, __LINE__, "ssdss", "unexpected type for key: ", cv[i].key, du->type, "(short)", "0 ... 65535");
				return -1;
			}
			break;
		case T_CONFIG_BOOLEAN:
			if (du->type == TYPE_STRING) {
				data_string *ds = (data_string *)du;
				
				if (0 == strcmp(ds->value->ptr, "enable")) {
					*((unsigned short *)(cv[i].destination)) = 1;
				} else if (0 == strcmp(ds->value->ptr, "disable")) {
					*((unsigned short *)(cv[i].destination)) = 0;
				} else {
					log_error_write(srv, __FILE__, __LINE__, "ssbs", "ERROR: unexpected value for key:", cv[i].key, ds->value, "(enable|disable)");
						
					return -1;
				}
			} else {
				log_error_write(srv, __FILE__, __LINE__, "ssss", "ERROR: unexpected type for key:", cv[i].key, "(string)", "\"(enable|disable)\"");
				
				return -1;
			}
			break;
		case T_CONFIG_LOCAL:
		case T_CONFIG_UNSET:
			break;
		case T_CONFIG_DEPRECATED:
			log_error_write(srv, __FILE__, __LINE__, "ssss", "ERROR: found deprecated key:", cv[i].key, "-", (char *)(cv[i].destination));
			
			srv->config_deprecated = 1;
			
			break;
		}
	}
	return 0;
}

static int config_insert(server *srv) {
	size_t i;
	int ret = 0;
	
	config_values_t cv[] = { 
		{ "server.bind",                 NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 0 */
		{ "server.errorlog",             NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 1 */
		{ "server.errorfile-prefix",     NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 2 */
		{ "server.chroot",               NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 3 */
		{ "server.username",             NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 4 */
		{ "server.groupname",            NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 5 */
		{ "server.port",                 NULL, T_CONFIG_SHORT,  T_CONFIG_SCOPE_SERVER },      /* 6 */
		{ "server.tag",                  NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },  /* 7 */
		{ "server.use-ipv6",             NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION }, /* 8 */
		{ "server.modules",              NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_SERVER },       /* 9 */
		
		{ "server.event-handler",        NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 10 */
		{ "server.pid-file",             NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 11 */
		{ "server.max-request-size",     NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },   /* 12 */
		{ "server.max-worker",           NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_SERVER },       /* 13 */
		{ "server.document-root",        NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },  /* 14 */
		{ "server.dir-listing",          NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION }, /* 15 */
		{ "server.indexfiles",           NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },   /* 16 */
		{ "server.max-keep-alive-requests", NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION }, /* 17 */
		{ "server.name",                 NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },  /* 18 */
		{ "server.max-keep-alive-idle",  NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },   /* 19 */
		
		{ "server.max-read-idle",        NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },   /* 20 */
		{ "server.max-write-idle",       NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },   /* 21 */
		{ "server.error-handler-404",    NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },  /* 22 */
		{ "server.max-fds",              NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_SERVER },       /* 23 */
		{ "server.follow-symlink",       NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION }, /* 24 */
		{ "server.kbytes-per-second",    NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },   /* 25 */
		{ "connection.kbytes-per-second", NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },  /* 26 */
		{ "mimetype.use-xattr",          NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_CONNECTION }, /* 27 */
		{ "mimetype.assign",             NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },   /* 28 */
		{ "ssl.pemfile",                 NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_SERVER },      /* 29 */
		
		{ "ssl.engine",                  NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_SERVER },     /* 30 */
		
		{ "debug.log-file-not-found",    NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_SERVER },     /* 31 */
		{ "debug.log-request-handling",  NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_SERVER },     /* 32 */
		{ "debug.log-response-header",   NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_SERVER },     /* 33 */
		{ "debug.log-request-header",    NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_SERVER },     /* 34 */
		
		{ "server.protocol-http11",      NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_SERVER },     /* 35 */
		{ "debug.log-request-header-on-error", NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_SERVER }, /* 36 */
		{ "debug.log-state-handling",    NULL, T_CONFIG_BOOLEAN, T_CONFIG_SCOPE_SERVER },     /* 37 */
		
		
		{ "server.host",                 "use server.bind instead", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_UNSET },
		{ "server.docroot",              "use server.document-root instead", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_UNSET },
		{ "server.virtual-root",         "load mod_simple_vhost and use simple-vhost.server-root instead", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_UNSET },
		{ "server.virtual-default-host", "load mod_simple_vhost and use simple-vhost.default-host instead", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_UNSET },
		{ "server.virtual-docroot",      "load mod_simple_vhost and use simple-vhost.document-root instead", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_UNSET },
		{ "server.userid",               "use server.username instead", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_UNSET },
		{ "server.groupid",              "use server.groupname instead", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_UNSET },
		{ "server.use-keep-alive",       "use server.max-keep-alive-requests = 0 instead", T_CONFIG_DEPRECATED, T_CONFIG_SCOPE_UNSET },
		
		{ NULL,                          NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET }
	};

	
	/* 0 */
	cv[0].destination = srv->srvconf.bindhost;
	cv[1].destination = srv->srvconf.error_logfile;
	cv[2].destination = srv->srvconf.errorfile_prefix;
	cv[3].destination = srv->srvconf.changeroot;
	cv[4].destination = srv->srvconf.username;
	cv[5].destination = srv->srvconf.groupname;
	cv[6].destination = &(srv->srvconf.port);
	
	cv[9].destination = srv->srvconf.modules;
	cv[10].destination = srv->srvconf.event_handler;
	cv[11].destination = srv->srvconf.pid_file;
	
	cv[13].destination = &(srv->srvconf.max_worker);
	cv[23].destination = &(srv->srvconf.max_fds);
	cv[36].destination = &(srv->srvconf.log_request_header_on_error);
	cv[37].destination = &(srv->srvconf.log_state_handling);
	
	srv->config_storage = malloc(srv->config_context->used * sizeof(specific_config *));

	assert(srv->config_storage);
	
	for (i = 0; i < srv->config_context->used; i++) {
		specific_config *s;
		
		s = calloc(1, sizeof(specific_config));
		assert(s);
		s->document_root = buffer_init();
		s->dir_listing   = 0;
		s->indexfiles    = array_init();
		s->mimetypes     = array_init();
		s->server_name   = buffer_init();
		s->ssl_pemfile   = buffer_init();
		s->error_handler = buffer_init();
		s->server_tag    = buffer_init();
		s->max_keep_alive_requests = 128;
		s->max_keep_alive_idle = 30;
		s->max_read_idle = 60;
		s->max_write_idle = 360;
		s->use_xattr     = 0;
		s->is_ssl        = 0;
		s->use_ipv6      = 0;
		s->follow_symlink = 1;
		s->kbytes_per_second = 0;
		s->allow_http11  = 1;
		s->global_kbytes_per_second = 0;
		s->global_bytes_per_second_cnt = 0;
		s->global_bytes_per_second_cnt_ptr = &s->global_bytes_per_second_cnt;
		
		cv[7].destination = s->server_tag;
		cv[8].destination = &(s->use_ipv6);
		
		
		cv[12].destination = &(s->max_request_size);
		/* 13 max-worker */
		cv[14].destination = s->document_root;
		cv[15].destination = &(s->dir_listing);
		cv[16].destination = s->indexfiles;
		cv[17].destination = &(s->max_keep_alive_requests);
		cv[18].destination = s->server_name;
		cv[19].destination = &(s->max_keep_alive_idle);
		cv[20].destination = &(s->max_read_idle);
		cv[21].destination = &(s->max_write_idle);
		cv[22].destination = s->error_handler;
		cv[24].destination = &(s->follow_symlink);
		/* 23 -> max-fds */
		cv[25].destination = &(s->global_kbytes_per_second);
		cv[26].destination = &(s->kbytes_per_second);
		cv[27].destination = &(s->use_xattr);
		cv[28].destination = s->mimetypes;
		cv[29].destination = s->ssl_pemfile;
		cv[30].destination = &(s->is_ssl);
		
		cv[31].destination = &(s->log_file_not_found);
		cv[32].destination = &(s->log_request_handling);
		cv[33].destination = &(s->log_response_header);
		cv[34].destination = &(s->log_request_header);
		
		cv[35].destination = &(s->allow_http11);
		
		srv->config_storage[i] = s;
	
		if (0 != (ret = config_insert_values_global(srv, ((data_config *)srv->config_context->data[i])->value, cv))) {
			break;
		}
	}
	
	return ret;
								 
}

int config_check_cond(server *srv, connection *con, data_config *dc) {
	buffer *l;
	server_socket *srv_sock = con->srv_socket;
	/* pass the rules */
	
	l = srv->empty_string;
	
	if (0 == strcmp(dc->comp_key->ptr, "HTTPhost")) {
		l = con->uri.authority;
	} else if (0 == strcmp(dc->comp_key->ptr, "HTTPurl")) {
		l = con->uri.path;
	} else if (0 == strcmp(dc->comp_key->ptr, "SERVERsocket")) {
		l = srv_sock->srv_token;
	} else if (0 == strcmp(dc->comp_key->ptr, "HTTPreferer")) {
		data_string *ds;
		
		if (NULL != (ds = (data_string *)array_get_element(con->request.headers, "Referer"))) {
			l = ds->value;
		}
	} else if (0 == strcmp(dc->comp_key->ptr, "HTTPcookie")) {
		data_string *ds;
		if (NULL != (ds = (data_string *)array_get_element(con->request.headers, "Cookie"))) {
			l = ds->value;
		}
	} else if (0 == strcmp(dc->comp_key->ptr, "HTTPuseragent")) {
		data_string *ds;
		if (NULL != (ds = (data_string *)array_get_element(con->request.headers, "User-Agent"))) {
			l = ds->value;
		}
	} else {
		return 0;
	}
	
	switch(dc->cond) {
	case CONFIG_COND_NE:
	case CONFIG_COND_EQ:
		if (buffer_is_equal(l, dc->match.string)) {
			return (dc->cond == CONFIG_COND_EQ) ? 1 : 0;
		} else {
			return (dc->cond == CONFIG_COND_EQ) ? 0 : 1;
		}
		break;
#ifdef HAVE_PCRE_H
	case CONFIG_COND_NOMATCH:
	case CONFIG_COND_MATCH: {
#define N 10
		int ovec[N * 3];
		int n;
		
		n = pcre_exec(dc->match.regex, NULL, l->ptr, l->used - 1, 0, 0, ovec, N * 3);
		
		if (n > 0) {
			return (dc->cond == CONFIG_COND_MATCH) ? 1 : 0;
		} else {
			return (dc->cond == CONFIG_COND_MATCH) ? 0 : 1;
		}
		
		break;
	}
#endif
	default:
		/* no way */
		break;
	}
	
	return 0;
}

#define PATCH(x) con->conf.x = s->x
int config_setup_connection(server *srv, connection *con) {
	specific_config *s = srv->config_storage[0];
	
	PATCH(allow_http11);
	PATCH(mimetypes);
	PATCH(document_root);
	PATCH(dir_listing);
	PATCH(indexfiles);
	PATCH(max_keep_alive_requests);
	PATCH(max_keep_alive_idle);
	PATCH(max_read_idle);
	PATCH(max_write_idle);
	PATCH(use_xattr);
	PATCH(error_handler);
	PATCH(follow_symlink);
	PATCH(server_tag);
	PATCH(kbytes_per_second);
	PATCH(global_kbytes_per_second);
	PATCH(global_bytes_per_second_cnt);
	con->conf.global_bytes_per_second_cnt_ptr = &s->global_bytes_per_second_cnt;
	buffer_copy_string_buffer(con->server_name, s->server_name);
	
	PATCH(log_request_header);
	PATCH(log_response_header);
	PATCH(log_request_handling);
	PATCH(log_file_not_found);
	
	return 0;
}

int config_patch_connection(server *srv, connection *con, const char *stage, size_t stage_len) {
	size_t i, j;
	
	/* skip the first, the global context */
	for (i = 1; i < srv->config_context->used; i++) {
		data_config *dc = (data_config *)srv->config_context->data[i];
		specific_config *s = srv->config_storage[i];
		
		/* not our stage */
		if (!buffer_is_equal_string(dc->comp_key, stage, stage_len)) continue;
		
		/* condition didn't match */
		if (!config_check_cond(srv, con, dc)) continue;
		
		/* merge config */
		for (j = 0; j < dc->value->used; j++) {
			data_unset *du = dc->value->data[j];
			
			if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.document-root"))) {
				PATCH(document_root);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.dir-listing"))) {
				PATCH(dir_listing);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.error-handler-404"))) {
				PATCH(error_handler);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.indexfiles"))) {
				PATCH(indexfiles);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("mimetype.assign"))) {
				PATCH(mimetypes);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.max-keep-alive-requests"))) {
				PATCH(max_keep_alive_requests);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.max-keep-alive-idle"))) {
				PATCH(max_keep_alive_idle);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.max-write-idle"))) {
				PATCH(max_write_idle);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.max-read-idle"))) {
				PATCH(max_read_idle);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("mimetype.use-xattr"))) {
				PATCH(use_xattr);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("ssl.pemfile"))) {
				PATCH(ssl_pemfile);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("ssl.engine"))) {
				PATCH(is_ssl);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.follow-symlink"))) {
				PATCH(follow_symlink);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.name"))) {
				buffer_copy_string_buffer(con->server_name, s->server_name);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.tag"))) {
				PATCH(server_tag);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("connection.kbytes-per-second"))) {
				PATCH(kbytes_per_second);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("debug.log-request-handling"))) {
				PATCH(log_request_handling);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("debug.log-request-header"))) {
				PATCH(log_request_header);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("debug.log-response-header"))) {
				PATCH(log_response_header);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("debug.log-file-not-found"))) {
				PATCH(log_file_not_found);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.protocol-http11"))) {
				PATCH(allow_http11);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.kbytes-per-second"))) {
				PATCH(global_kbytes_per_second);
				PATCH(global_bytes_per_second_cnt);
				con->conf.global_bytes_per_second_cnt_ptr = &s->global_bytes_per_second_cnt;
			}
		}
	}
	
	return 0;
}
#undef PATCH
typedef struct {
	int foo;
	int bar;
	
	char *input;
	size_t offset;
	size_t size;
	
	int line_pos;
	int line;
	
	int in_key;
	int in_brace;
	int in_cond;
} tokenizer_t;

static int config_tokenizer(server *srv, tokenizer_t *t, int *token_id, buffer *token) {
	int tid = 0;
	size_t i;
	
	for (tid = 0; tid == 0 && t->offset < t->size && t->input[t->offset] ; ) {
		char c = t->input[t->offset];
		char *start = NULL;
		
		switch (c) {
		case '=': 
			if (t->in_brace) {
				if (t->input[t->offset + 1] == '>') {
					t->offset += 2;
					
					buffer_copy_string(token, "=>");
					
					tid = TK_ARRAY_ASSIGN;
				} else {
					log_error_write(srv, __FILE__, __LINE__, "sdsds", 
							"line:", t->line, "pos:", t->line_pos, 
							"use => for assignments in arrays");
					return -1;
				}
			} else if (t->in_cond) {
				if (t->input[t->offset + 1] == '=') {
					t->offset += 2;
					
					buffer_copy_string(token, "==");
					
					tid = TK_EQ;
				} else if (t->input[t->offset + 1] == '~') {
					t->offset += 2;
					
					buffer_copy_string(token, "=~");
					
					tid = TK_MATCH;
				} else {
					log_error_write(srv, __FILE__, __LINE__, "sdsds", 
							"line:", t->line, "pos:", t->line_pos, 
							"only =~ and == are allow in the condition");
					return -1;
				}
			} else if (t->in_key) {
				tid = TK_ASSIGN;
				
				buffer_copy_string_len(token, t->input + t->offset, 1);
				
				t->offset++;
				t->line_pos++;
				t->in_key = 0;
			} else {
				log_error_write(srv, __FILE__, __LINE__, "sdsds", 
						"line:", t->line, "pos:", t->line_pos, 
						"unexpected equal-sign: =");
				return -1;
			}
			
			break;
		case '!': 
			if (t->in_cond) {
				if (t->input[t->offset + 1] == '=') {
					t->offset += 2;
					
					buffer_copy_string(token, "!=");
					
					tid = TK_NE;
				} else if (t->input[t->offset + 1] == '~') {
					t->offset += 2;
					
					buffer_copy_string(token, "!~");
					
					tid = TK_NOMATCH;
				} else {
					log_error_write(srv, __FILE__, __LINE__, "sdsds", 
							"line:", t->line, "pos:", t->line_pos, 
							"only !~ and != are allow in the condition");
					return -1;
				}
			} else {
				log_error_write(srv, __FILE__, __LINE__, "sdsds", 
						"line:", t->line, "pos:", t->line_pos, 
						"unexpected exclamation-marks: !");
				return -1;
			}
			
			break;
		case '\t':
		case ' ':
			t->offset++;
			t->line_pos++;
			break;
		case '\r':
			if (t->in_brace == 0) {
				if (t->input[t->offset + 1] == '\n') {
					t->in_key = 1;
					t->offset += 2;
					
					tid = TK_EOL;
					t->line++;
					t->line_pos = 1;
					
					buffer_copy_string(token, "(EOL)");
				} else {
					log_error_write(srv, __FILE__, __LINE__, "sdsds", 
							"line:", t->line, "pos:", t->line_pos, 
							"CR without LF");
					return 0;
				}
			} else {
				t->offset++;
				t->line_pos++;
			}
			break;
		case '\n':
			if (t->in_brace == 0) {
				t->in_key = 1;
				
				tid = TK_EOL;
				
				buffer_copy_string(token, "(EOL)");
			}
			t->line++;
			t->line_pos = 1;
			t->offset++;
			break;
		case ',':
			if (t->in_brace > 0) {
				tid = TK_COMMA;
				
				buffer_copy_string(token, "(COMMA)");
			}
			
			t->offset++;
			t->line_pos++;
			break;
		case '"':
			/* search for the terminating " */
			start = t->input + t->offset + 1;
			buffer_copy_string(token, "");
			
			for (i = 1; t->input[t->offset + i]; i++) {
				if (t->input[t->offset + i] == '\\' &&
				    t->input[t->offset + i + 1] == '"') {
					
					buffer_append_string_len(token, start, t->input + t->offset + i - start);
					
					start = t->input + t->offset + i + 1;
					
					/* skip the " */
					i++;
					continue;
				}
				
				
				if (t->input[t->offset + i] == '"') {
					tid = TK_STRING;
				
					buffer_append_string_len(token, start, t->input + t->offset + i - start);
					
					break;
				}
			}

			if (t->input[t->offset + i] == '\0') {
				/* ERROR */
				
				log_error_write(srv, __FILE__, __LINE__, "sdsds", 
						"line:", t->line, "pos:", t->line_pos, 
						"missing closing quote");
				
				return -1;
			}
			
			t->offset += i + 1;
			t->line_pos += i + 1;
			
			break;
		case '(':
			t->offset++;
			t->in_brace++;
				
			tid = TK_LPARAN;
				
			buffer_copy_string(token, "(");
			break;
		case ')':
			t->offset++;
			t->in_brace--;
				
			tid = TK_RPARAN;
				
			buffer_copy_string(token, ")");
			break;
		case '$':
			t->offset++;
				
			tid = TK_DOLLAR;
			t->in_cond = 1;
			t->in_key = 0;
				
			buffer_copy_string(token, "$");
			
			break;
		case '{':
			t->offset++;
				
			tid = TK_LCURLY;
			t->in_key = 1;
			t->in_cond = 0;
				
			buffer_copy_string(token, "{");
			
			break;
			
		case '}':
			t->offset++;
				
			tid = TK_RCURLY;
				
			buffer_copy_string(token, "}");
			
			break;
		case '[':
			t->offset++;
				
			tid = TK_LBRACKET;
				
			buffer_copy_string(token, "[");
			
			break;
			
		case ']':
			t->offset++;
				
			tid = TK_RBRACKET;
				
			buffer_copy_string(token, "]");
			
			break;
		case '#':
			for (i = 1; t->input[t->offset + i] && 
			     (t->input[t->offset + i] != '\n' && t->input[t->offset + i] != '\r');
			     i++);
			
			t->offset += i;
			
			break;
		default:
			if (t->in_key) {
				/* the key might consist of [-.0-9a-z] */
				for (i = 0; t->input[t->offset + i] && 
				     (isalnum((unsigned char)t->input[t->offset + i]) || 
				      t->input[t->offset + i] == '.' ||
				      t->input[t->offset + i] == '-'
				      ); i++);
				
				if (i && t->input[t->offset + i]) {
					tid = TK_LKEY;
					buffer_copy_string_len(token, t->input + t->offset, i);
					
					t->offset += i;
					t->line_pos += i;
				} else {
					/* ERROR */
					log_error_write(srv, __FILE__, __LINE__, "sdsds", 
							"line:", t->line, "pos:", t->line_pos, 
							"invalid character in lvalue");
					return -1;
				}
			} else if (t->in_cond) {
				for (i = 0; t->input[t->offset + i] && 
				     (isalpha((unsigned char)t->input[t->offset + i])
				      ); i++);
				
				if (i && t->input[t->offset + i]) {
					tid = TK_SRVVARNAME;
					buffer_copy_string_len(token, t->input + t->offset, i);
					
					t->offset += i;
					t->line_pos += i;
				} else {
					/* ERROR */
					log_error_write(srv, __FILE__, __LINE__, "sdsds", 
							"line:", t->line, "pos:", t->line_pos, 
							"invalid character in condition");
					return -1;
				}
			} else {
				if (isdigit((unsigned char)c)) {
					/* take all digits */
					for (i = 0; t->input[t->offset + i] && isdigit((unsigned char)t->input[t->offset + i]);  i++);
					
					/* was there it least a digit ? */
					if (i && t->input[t->offset + i]) {
						tid = TK_INTEGER;
						
						buffer_copy_string_len(token, t->input + t->offset, i);
						
						t->offset += i;
						t->line_pos += i;
					} else {
						/* ERROR */
						log_error_write(srv, __FILE__, __LINE__, "sdsds", 
								"line:", t->line, "pos:", t->line_pos, 
								"unexpected EOF");
						
						return -1;
					}
				} else {
					/* ERROR */
					log_error_write(srv, __FILE__, __LINE__, "sdsds", 
							"line:", t->line, "pos:", t->line_pos, 
							"invalid value field");
					
					return -1;
				}
			}
			break;
		}
	}
	
	if (tid) {
		*token_id = tid;
		
		return 1;
	} else if (t->offset < t->size) {
		fprintf(stderr, "%s.%d: %d, %s\n",
			__FILE__, __LINE__,
			tid, token->ptr);
	}
	return 0;
}

int config_read(server *srv, const char *fn) {
	stream s;
	tokenizer_t t;
	void *pParser;
	int token_id;
	buffer *token;
	config_t context;
	data_config *dc;
	int ret;
	buffer *bfn = buffer_init_string(fn);
	
	if (0 != stream_open(&s, bfn)) {
		buffer_free(bfn);
		
		log_error_write(srv, __FILE__, __LINE__, "ssss", 
				"opening configfile ", fn, "failed:", strerror(errno));
		return -1;
	}
	
	buffer_free(bfn);

	t.input = s.start;
	t.offset = 0;
	t.size = s.size;
	t.line = 1;
	t.line_pos = 1;
	
	t.in_key = 1;
	t.in_brace = 0;
	t.in_cond = 0;
	
	context.ok = 1;
	context.config = srv->config_context;
	
	dc = data_config_init();
	buffer_copy_string(dc->key, "global");
	array_insert_unique(srv->config_context, (data_unset *)dc);
	
	context.ctx_name = dc->key;
	context.ctx_config = dc->value;
	
	/* default context */
	srv->config = dc->value;
	
	pParser = configparserAlloc( malloc );
	token = buffer_init();
	while((1 == (ret = config_tokenizer(srv, &t, &token_id, token))) && context.ok) {
		configparser(pParser, token_id, token, &context);
		
		token = buffer_init();
	}
	configparser(pParser, 0, token, &context);
	configparserFree(pParser, free );
	
	buffer_free(token);
	
	stream_close(&s);
	
	if (ret == -1) {
		log_error_write(srv, __FILE__, __LINE__, "s", 
				"configfile parser failed");
		return -1;
	}
	
	if (context.ok == 0) {
		log_error_write(srv, __FILE__, __LINE__, "sdsds", 
				"line:", t.line, "pos:", t.line_pos, 
				"parser failed somehow near here");
		return -1;
	}
	
	if (0 != config_insert(srv)) {
		return -1;
	}
	
	if (NULL != (dc = (data_config *)array_get_element(srv->config_context, "global"))) {
		srv->config = dc->value;
	} else {
		return -1;
	}
	
	return 0;
}

int config_set_defaults(server *srv) {
	size_t i;
	specific_config *s = srv->config_storage[0];
	
	struct ev_map { fdevent_handler_t et; const char *name; } event_handlers[] = 
	{ 
		/* - poll is most reliable
		 * - select works everywhere
		 * - linux-* are experimental
		 */
#ifdef USE_POLL
		{ FDEVENT_HANDLER_POLL,           "poll" },
#endif
#ifdef USE_SELECT
		{ FDEVENT_HANDLER_SELECT,         "select" },
#endif
#ifdef USE_LINUX_EPOLL
		{ FDEVENT_HANDLER_LINUX_SYSEPOLL, "linux-sysepoll" },
#endif
#ifdef USE_LINUX_SIGIO
		{ FDEVENT_HANDLER_LINUX_RTSIG,    "linux-rtsig" },
#endif
#ifdef USE_SOLARIS_DEVPOLL
		{ FDEVENT_HANDLER_SOLARIS_DEVPOLL,"solaris-devpoll" },
#endif
#ifdef USE_FREEBSD_KQUEUE
		{ FDEVENT_HANDLER_FREEBSD_KQUEUE, "freebsd-kqueue" },
#endif
		{ FDEVENT_HANDLER_UNSET,          NULL }
	};
	
#ifdef USE_LICENSE
	license_t *l;
	
	if (srv->srvconf.license->used == 0) {
		/* license is missing */
		return -1;
	}

	l = license_init();
	
	if (0 != license_parse(l, srv->srvconf.license)) {
		log_error_write(srv, __FILE__, __LINE__, "sb", 
				"parsing license information failed", srv->srvconf.license);
		
		license_free(l);
		return -1;
	}
	if (!license_is_valid(l)) {
		log_error_write(srv, __FILE__, __LINE__, "s", 
				"license is not valid");
		
		license_free(l);
		return -1;
	}
	license_free(l);
#endif	
	if (srv->srvconf.port == 0) {
		srv->srvconf.port = s->is_ssl ? 443 : 80;
	}
	
	if (srv->srvconf.event_handler->used == 0) {
		/* choose a good default
		 * 
		 * the event_handler list is sorted by 'goodness' 
		 * taking the first available should be the best solution
		 */
		srv->event_handler = event_handlers[0].et;
		
		if (FDEVENT_HANDLER_UNSET == srv->event_handler) {
			log_error_write(srv, __FILE__, __LINE__, "s", 
					"sorry, there is no event handler for this system");
			
			return -1;
		}
	} else {
		/*
		 * User override
		 */
		
		for (i = 0; event_handlers[i].name; i++) {
			if (0 == strcmp(event_handlers[i].name, srv->srvconf.event_handler->ptr)) {
				srv->event_handler = event_handlers[i].et;
				break;
			}
		}
		
		if (FDEVENT_HANDLER_UNSET == srv->event_handler) {
			log_error_write(srv, __FILE__, __LINE__, "sb", 
					"the selected event-handler in unknown or not supported:", 
					srv->srvconf.event_handler );
			
			return -1;
		}
	}

	if (s->is_ssl) {
		if (s->ssl_pemfile->used == 0) {
			/* PEM file is require */
			
			log_error_write(srv, __FILE__, __LINE__, "s", 
					"ssl.pemfile has to be set");
			return -1;
		}
		
#ifndef USE_OPENSSL
		log_error_write(srv, __FILE__, __LINE__, "s", 
				"ssl support is missing, recompile with --with-openssl");
		
		return -1;
#endif
	}
	
	return 0;
}
