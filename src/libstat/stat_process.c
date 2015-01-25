/* Copyright (c) 2015, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "stat_api.h"
#include "main.h"
#include "stat_internal.h"
#include "message.h"
#include "lua/lua_common.h"
#include <utlist.h>

struct rspamd_tokenizer_runtime {
	GTree *tokens;
	const gchar *name;
	struct rspamd_stat_tokenizer *tokenizer;
	struct rspamd_tokenizer_runtime *next;
};

struct preprocess_cb_data {
	GList *classifier_runtimes;
	guint results_count;
};

static gboolean
preprocess_init_stat_token (gpointer k, gpointer v, gpointer d)
{
	rspamd_token_t *t = (rspamd_token_t *)v;
	struct preprocess_cb_data *cbdata = (struct preprocess_cb_data *)d;
	struct rspamd_statfile_runtime *st_runtime;
	struct rspamd_classifier_runtime *cl_runtime;
	struct rspamd_token_result *res;
	GList *cur, *curst;
	gint i = 0;

	t->results = g_array_sized_new (FALSE, TRUE,
			sizeof (struct rspamd_token_result), cbdata->results_count);

	cur = g_list_first (cbdata->classifier_runtimes);

	while (cur) {
		cl_runtime = (struct rspamd_classifier_runtime *)cur->data;
		res = &g_array_index (t->results, struct rspamd_token_result, i);

		curst = res->cl_runtime->st_runtime;

		while (curst) {
			st_runtime = (struct rspamd_statfile_runtime *)curst->data;

			res->cl_runtime = cl_runtime;
			res->st_runtime = st_runtime;

			if (st_runtime->backend->process_token (t, res,
					st_runtime->backend->ctx)) {
				cl_runtime->processed_tokens ++;
			}

			i ++;
			curst = g_list_next (curst);
		}
		cur = g_list_next (cur);
	}


	return FALSE;
}

static GList*
rspamd_stat_preprocess (struct rspamd_stat_ctx *st_ctx,
		struct rspamd_task *task, struct rspamd_tokenizer_runtime *tklist,
		lua_State *L, GError **err)
{
	struct rspamd_classifier_config *clcf;
	struct rspamd_statfile_config *stcf;
	struct rspamd_tokenizer_runtime *tok;
	struct rspamd_classifier_runtime *cl_runtime;
	struct rspamd_statfile_runtime *st_runtime;
	struct rspamd_stat_backend *bk;
	gpointer backend_runtime;
	GList *cur, *st_list = NULL, *curst;
	GList *cl_runtimes = NULL;
	guint result_size = 0;
	struct preprocess_cb_data cbdata;

	cur = g_list_first (task->cfg->classifiers);

	while (cur) {
		clcf = (struct rspamd_classifier_config *)cur->data;

		if (clcf->pre_callbacks != NULL) {
			st_list = rspamd_lua_call_cls_pre_callbacks (clcf, task, FALSE,
					FALSE, L);
		}
		if (st_list != NULL) {
			rspamd_mempool_add_destructor (task->task_pool,
					(rspamd_mempool_destruct_t)g_list_free, st_list);
		}
		else {
			st_list = clcf->statfiles;
		}

		/* Now init runtime values */
		cl_runtime = rspamd_mempool_alloc0 (task->task_pool, sizeof (*cl_runtime));
		cl_runtime->cl = rspamd_stat_get_classifier (clcf->classifier);

		if (cl_runtime->cl == NULL) {
			g_set_error (err, rspamd_stat_quark(), 500,
					"classifier %s is not defined", clcf->classifier);
			g_list_free (cl_runtimes);
			return NULL;
		}

		cl_runtime->clcf = clcf;

		curst = clcf->statfiles;
		while (curst != NULL) {
			stcf = (struct rspamd_statfile_config *)curst->data;

			bk = rspamd_stat_get_backend (stcf->backend);

			if (bk == NULL) {
				msg_warn ("backend of type %s is not defined", stcf->backend);
				curst = g_list_next (curst);
				continue;
			}

			backend_runtime = bk->runtime (stcf, bk->ctx);

			st_runtime = rspamd_mempool_alloc0 (task->task_pool,
					sizeof (*st_runtime));
			st_runtime->st = stcf;
			st_runtime->backend_runtime = backend_runtime;
			st_runtime->backend = bk;

			cl_runtime->st_runtime = g_list_prepend (cl_runtime->st_runtime,
					st_runtime);
			result_size ++;

			curst = g_list_next (curst);
		}

		if (cl_runtime->st_runtime != NULL) {
			rspamd_mempool_add_destructor (task->task_pool,
					(rspamd_mempool_destruct_t)g_list_free,
					cl_runtime->st_runtime);
			cl_runtimes = g_list_prepend (cl_runtimes, cl_runtime);
		}

		cur = g_list_next (cur);
	}

	if (cl_runtimes != NULL) {
		rspamd_mempool_add_destructor (task->task_pool,
				(rspamd_mempool_destruct_t)g_list_free,
				cl_runtimes);

		cbdata.results_count = result_size;
		cbdata.classifier_runtimes = cl_runtimes;

		/* Allocate token results */
		LL_FOREACH (tklist, tok) {
			g_tree_foreach (tok->tokens, preprocess_init_stat_token, &cbdata);
		}
	}

	return cl_runtimes;
}

static struct rspamd_tokenizer_runtime *
rspamd_stat_get_tokenizer_runtime (const gchar *name, rspamd_mempool_t *pool,
		struct rspamd_tokenizer_runtime **ls)
{
	struct rspamd_tokenizer_runtime *tok = NULL, *cur;

	LL_FOREACH (*ls, cur) {
		if (strcmp (cur->name, name) == 0) {
			tok = cur;
			break;
		}
	}

	if (tok == NULL) {
		tok = rspamd_mempool_alloc (pool, sizeof (*tok));
		tok->tokenizer = rspamd_stat_get_tokenizer (name);

		if (tok->tokenizer == NULL) {
			return NULL;
		}

		tok->tokens = g_tree_new (token_node_compare_func);
		rspamd_mempool_add_destructor (pool,
				(rspamd_mempool_destruct_t)g_tree_destroy, tok->tokens);
		tok->name = name;
		LL_PREPEND(*ls, tok);
	}

	return tok;
}

/*
 * Tokenize task using the tokenizer specified
 */
static void
rspamd_stat_process_tokenize (struct rspamd_stat_ctx *st_ctx,
		struct rspamd_task *task, struct rspamd_tokenizer_runtime *tok)
{
	struct mime_text_part *part;
	GArray *words;
	gchar *sub;
	GList *cur;

	cur = task->text_parts;

	while (cur != NULL) {
		part = (struct mime_text_part *)cur->data;

		if (!part->is_empty && part->words != NULL) {
			/*
			 * XXX: Use normalized words if needed here
			 */
			tok->tokenizer->tokenize_func (tok->tokenizer, task->task_pool,
					part->words, tok->tokens, part->is_utf);
		}

		cur = g_list_next (cur);
	}

	if (task->subject != NULL) {
		sub = task->subject;
	}
	else {
		sub = (gchar *)g_mime_message_get_subject (task->message);
	}

	if (sub != NULL) {
		words = rspamd_tokenize_text (sub, strlen (sub), TRUE, 0, NULL);
		if (words != NULL) {
			tok->tokenizer->tokenize_func (tok->tokenizer,
					task->task_pool,
					words,
					tok->tokens,
					TRUE);
			g_array_free (words, TRUE);
		}
	}
}


gboolean
rspamd_stat_classify (struct rspamd_task *task, lua_State *L, GError **err)
{
	struct rspamd_stat_classifier *cls;
	struct rspamd_classifier_config *clcf;
	GList *cur;
	struct rspamd_stat_ctx *st_ctx;
	struct rspamd_tokenizer_runtime *tklist = NULL, *tok;
	GList *cl_runtimes;


	st_ctx = rspamd_stat_get_ctx ();
	g_assert (st_ctx != NULL);

	cur = g_list_first (task->cfg->classifiers);

	/* Tokenization */
	while (cur) {
		clcf = (struct rspamd_classifier_config *)cur->data;
		cls = rspamd_stat_get_classifier (clcf->classifier);

		if (cls == NULL) {
			g_set_error (err, rspamd_stat_quark (), 500, "type %s is not defined"
					"for classifiers", clcf->classifier);
			return FALSE;
		}

		tok = rspamd_stat_get_tokenizer_runtime (clcf->tokenizer, task->task_pool,
				&tklist);

		if (tok == NULL) {
			g_set_error (err, rspamd_stat_quark (), 500, "type %s is not defined"
					"for tokenizers", clcf->tokenizer);
			return FALSE;
		}

		rspamd_stat_process_tokenize (st_ctx, task, tok);

		cur = g_list_next (cur);
	}

	/* Initialize classifiers and statfiles runtime */
	if ((cl_runtimes = rspamd_stat_preprocess (st_ctx, task, tklist, L, err))
			== NULL) {
		return FALSE;
	}

	return TRUE;
}
