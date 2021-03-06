/*********************************************************
 * *Copyright (C) 2015 Neil Horman
 * *This program is free software; you can redistribute it and\or modify
 * *it under the terms of the GNU General Public License as published 
 * *by the Free Software Foundation; either version 2 of the License,
 * *or  any later version.
 * *
 * *This program is distributed in the hope that it will be useful,
 * *but WITHOUT ANY WARRANTY; without even the implied warranty of
 * *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * *GNU General Public License for more details.
 * *
 * *File: postgres-db.c
 * *
 * *Author:Neil Horman
 * *
 * *Date:
 * *
 * *Description implements access to postgres db 
 * *********************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <libpq-fe.h>
#include <string.h>
#include <freight-common.h>
#include <freight-db.h>

struct postgres_info {
	PGconn *conn;
};

static int pg_init(struct agent_config *acfg)
{
	struct postgres_info *info = calloc(1, sizeof(struct postgres_info));
	if (!info)
		return -ENOMEM;
	acfg->db.db_priv = info;
	return 0;
}

static void pg_cleanup(struct agent_config *acfg)
{
	struct postgres_info *info = acfg->db.db_priv;

	free(info);
	acfg->db.db_priv = NULL;
	return;
}

static int pg_disconnect(struct agent_config *acfg)
{
	struct postgres_info *info = acfg->db.db_priv;
	PQfinish(info->conn);
	return 0;
}

static int pg_connect(struct agent_config *acfg)
{
	int rc = -ENOTCONN;

	struct postgres_info *info = acfg->db.db_priv;

	char * k[] = {
		"hostaddr",
		"dbname",
		"user",
		"password",
		NULL
	};
	const char *const * keywords = (const char * const *)k;

	char *v[] = {
		acfg->db.hostaddr,
		acfg->db.dbname,
		acfg->db.user,
		acfg->db.password,
		NULL
	};

	const char *const * values = (const char * const *)v;

	info->conn = PQconnectdbParams(keywords, values, 0);

	if (PQstatus(info->conn) == CONNECTION_OK)
		LOG(INFO, "freight-agent connection...Established!\n");
	else {
		LOG(INFO, "freight-agent connection...Failed: %s\n",
			PQerrorMessage(info->conn));
		pg_disconnect(acfg);
		goto out;
	}	
	rc = 0;

out:
	return rc;
}


static int pg_send_raw_sql(const char *sql,
		           const struct agent_config *acfg)
{
	struct postgres_info *info = acfg->db.db_priv;
	PGresult *result;
	ExecStatusType rc;
	int retc = -EINVAL;

	result = PQexec(info->conn, sql);

	rc = PQresultStatus(result);
	if (rc != PGRES_COMMAND_OK) {
		LOG(ERROR, "Unable to execute sql: %s\n",
			PQresultErrorMessage(result));
		goto out;
	}

	retc = 0;
out:
	return retc;
}

static struct tbl* pg_get_table(const char *table,
			 const char *cols,
			 const char *filter,
			 const struct agent_config *acfg)
{
	struct postgres_info *info = acfg->db.db_priv;
	PGresult *result;
	ExecStatusType rc;
	int row, col, r, c;
	char *sql;
	struct tbl *rtable = NULL;

	sql = strjoina("SELECT ", cols, " FROM ", table, " WHERE ", filter);
	result = PQexec(info->conn, sql);

	rc = PQresultStatus(result);
	if (rc != PGRES_TUPLES_OK) {
		LOG(ERROR, "Unable to query %s table: %s\n",
			table, PQresultErrorMessage(result));
		goto out;
	}

	row = PQntuples(result);
	col = PQnfields(result);

	rtable = alloc_tbl(row, col);
	if (!rtable)
		goto out_clear;
	
	/*
 	 * Loop through the result and copy out the table data
 	 * Column 0 is the repo name
 	 * Column 1 is the repo url
 	 */
	for (r = 0; r < row; r++) { 
		for (c = 0; c < col; c++) {
			rtable->value[r][c] = strdup(PQgetvalue(result, r, c));
			if (!rtable->value[r][c]) {
				free_tbl(rtable);
				rtable = NULL;
				goto out_clear;
			}
		}
	}


out_clear:
	PQclear(result);
out:		
	return rtable;
}

struct db_api postgres_db_api = {
	.init = pg_init,
	.cleanup = pg_cleanup,
	.connect = pg_connect,
	.disconnect = pg_disconnect,
	.send_raw_sql = pg_send_raw_sql,
	.get_table = pg_get_table,
};
