/*
 *  chap-mysql.c -- Challenge Handshake Authentication Protocol for the
 *                  Point-to-Point Protocol (PPP) via MySQL.
 *
 *  Copyright (c) 2008 Maik Broemme <mbroemme@plusserver.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* generic includes. */
#include <string.h>

/* generic plugin includes. */
#include "plugin-mysql.h"
#include "str.h"

/* chap plugin includes. */
#include "chap-mysql.h"

/* this function check the chap authentication information against a mysql database. */
int32_t pppd__chap_verify_mysql(char *name, char *ourname, int id, struct chap_digest_type *digest, unsigned char *challenge, unsigned char *response, char *message, int message_space) {

	/* some common variables. */
	uint8_t *token_mysql_uri;
	uint8_t *token_mysql_host;
	uint8_t *token_mysql_port;
	uint8_t query[1024];
	uint8_t query_extended[1024];
	uint8_t secret_name[MAXSECRETLEN];
	int32_t secret_length	= 0;
	int32_t ok		= 0;
	uint32_t columns	= 0;
	uint32_t count		= 0;
	uint32_t found		= 0;
	MYSQL_RES *result	= NULL;
	MYSQL_ROW row		= NULL;
	MYSQL_FIELD *field	= NULL;
	MYSQL mysql;

	/* check if all information are supplied. */
	if (pppd_mysql_host		== NULL ||
	    pppd_mysql_database		== NULL ||
	    pppd_mysql_user		== NULL ||
	    pppd_mysql_pass		== NULL ||
	    pppd_mysql_table		== NULL ||
	    pppd_mysql_column_user	== NULL ||
	    pppd_mysql_column_pass	== NULL ||
	    pppd_mysql_column_ip	== NULL) {

		/* something failed on mysql initialization. */
		error("Plugin %s: The MySQL information are not complete\n", PLUGIN_NAME_MYSQL);

		/* return with error and terminate link. */
		return 0;
	}

	/* check if mysql initialization was successful. */
	if (mysql_init(&mysql) == NULL) {

		/* something failed on mysql initialization. */
		error("Plugin %s: The MySQL initialization failed\n", PLUGIN_NAME_MYSQL);

		/* return with error and terminate link. */
		return 0;
	}

	/* set mysql connect timeout. */
	if (mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, (uint8_t *)&pppd_mysql_connect_timeout) != 0) {

		info("Plugin %s: Some MySQL options are unknown\n", PLUGIN_NAME_MYSQL);

		/* return with error and terminate link. */
		return 0;
	}

	/* loop through all server tokens. */
	while ((token_mysql_uri = pppd__strsep(&pppd_mysql_host, ",")) != NULL) {

		/* extract host and port. */
		token_mysql_host = pppd__strsep(&token_mysql_uri, ":");
		token_mysql_port = pppd__strsep(&token_mysql_uri, ":");

		/* strip away leading and trailing whitespaces. */
		token_mysql_host = pppd__strstrip(token_mysql_host);
		token_mysql_port = pppd__strstrip(token_mysql_port);

		/* loop through number of connection retries. */
		for (count = pppd_mysql_retry_connect; count > 0 ; count--) {

			/* check if mysql connection was successfully established. */
			if (mysql_real_connect(&mysql, token_mysql_host, pppd_mysql_user, pppd_mysql_pass, pppd_mysql_database, (uint32_t)atoi (token_mysql_port ? token_mysql_port: 0), (uint8_t *)NULL, 0) == 0) {

				/* check if it was last connection try. */
				if (count == 1) {

					/* something on establishing connection failed. */
					warn("Plugin %s: Warning MySQL server %s not working\n", PLUGIN_NAME_MYSQL, token_mysql_host);
				}
			} else {

				/* found working mysql server. */
				info("Plugin %s: Using MySQL server %s\n", PLUGIN_NAME_MYSQL, token_mysql_host);

				/* indicate that we found working mysql server. */
				found = 1;

				/* found working connection, so break loop. */
				break;
			}
		}

		/* check if we found working mysql server. */
		if (found == 1) {

			/* found working connection, so break loop. */
			break;
		}
	}

	/* check if no connection was established, very bad :) */
	if (found == 0) {

		/* something on establishing connection failed. */
		error("Plugin %s: No working MySQL server found\n", PLUGIN_NAME_MYSQL);

		/* close the connection. */
		mysql_close(&mysql);

		/* return with error and terminate link. */
		return 0;
	}

	/* build query for database. */
	snprintf(query, 1024, "SELECT %s, %s FROM %s WHERE %s='%s'", pppd_mysql_column_pass, pppd_mysql_column_ip, pppd_mysql_table, pppd_mysql_column_user, name);

	/* check if we have an additional mysql condition. */
	if (pppd_mysql_condition != NULL) {

		/* build extended query for database. */
		snprintf(query_extended, 1024, " AND %s", pppd_mysql_condition);

		/* only write 1023 bytes, because strncat writes 1023 bytes plus the terminating null byte. */
		strncat(query, query_extended, 1023);
	}

	/* set successful execution value to zero. */
	found = 0;

	/* loop through number of connection retries. */
	for (count = pppd_mysql_retry_query; count > 0 ; count--) {

		/* check if query was successfully executed. */
		if (mysql_query(&mysql, query) == 0) {

			/* indicate that we fetch a result. */
			found = 1;

			/* query result was ok, so break loop. */
			break;
		}
	}

	/* check if no query was executed successfully, very bad :) */
	if (found == 0) {

		/* something on executing query failed. */
		error("Plugin %s: ERROR %d (%s): %s\n", PLUGIN_NAME_MYSQL, mysql_errno(&mysql), mysql_sqlstate(&mysql), mysql_error(&mysql));

		/* close the connection. */
		mysql_close(&mysql);

		/* return with error and terminate link. */
		return 0;
	}

	/* check if mysql result was successfully stored. */
	if ((result = mysql_store_result(&mysql)) == NULL) {

		/* check if mysql should return data. */
		if (mysql_field_count(&mysql) == 0) {

			/* something on executing query failed. */
			printf("Plugin %s: ERROR %d (%s): %s\n", PLUGIN_NAME_MYSQL, mysql_errno(&mysql), mysql_sqlstate(&mysql), mysql_error(&mysql));

			/* close the connection. */
			mysql_close(&mysql);

			/* return with error and terminate link. */
			return 0;
		}
	}

	/* check if we have multiple user accounts. */
	if ((mysql_num_rows(result) > 1) && (pppd_mysql_ignore_multiple == 0)) {

		/* multiple user accounts found. */
		error("Plugin %s: Multiple accounts for %s found in database\n", PLUGIN_NAME_MYSQL, name);

		/* close the connection. */
		mysql_close(&mysql);

		/* return with error and terminate link. */
		return 0;
	}

	/* fetch number of columns. */
	columns = mysql_num_fields(result);

	/* fetch mysql row, we only take care of first row. */
	row = mysql_fetch_row(result);

	/* loop through all columns. */
	for (count = 0; count < columns; count++) {

		/* fetch mysql field name. */
		field = mysql_fetch_field(result);

		/* check if column is NULL. */
		if ((row[count] == NULL) && (pppd_mysql_ignore_null == 0)) {

			/* multiple user accounts found. */
			error("Plugin %s: The column %s for %s is NULL in database\n", PLUGIN_NAME_MYSQL, field->name, name);

			/* clear the memory with the password, so nobody is able to dump it. */
			memset(secret_name, 0, sizeof(secret_name));

			/* close the connection. */
			mysql_close(&mysql);

			/* return with error and terminate link. */
			return 0;
		}

		/* if we reach this point, check only if column is NULL and transform it. */
		row[count] = row[count] ? row[count] : "NULL";

		/* check if we found password. */
		if (strcmp(field->name, pppd_mysql_column_pass) == 0) {

			/* cleanup memory. */
			memset(secret_name, 0, sizeof(secret_name));

			/* copy password to secret. */
			strncpy(secret_name, row[count], MAXSECRETLEN);
			secret_length = strlen(secret_name);
		}

		/* check if we found client ip. */
		if (strcmp(field->name, pppd_mysql_column_ip) == 0) {

			/* check if ip address was successfully converted into binary data. */
			if (inet_aton(row[count], (struct in_addr *) &client_ip) == 0) {

				/* error on converting ip address.*/
				error("Plugin %s: IP address %s is not valid\n", PLUGIN_NAME_MYSQL, row[count]);

				/* clear the memory with the password, so nobody is able to dump it. */
				memset(secret_name, 0, sizeof(secret_name));

				/* close the connection. */
				mysql_close(&mysql);

				/* return with error and terminate link. */
				return 0;
			}
		}
	}

	/* close the connection. */
	mysql_close(&mysql);

	/* check the discovered secret against the client's response. */
	ok = digest->verify_response(id, name, secret_name, secret_length, challenge, response, message, message_space);

	/* clear the memory with the password, so nobody is able to dump it. */
	memset(secret_name, 0, sizeof(secret_name));

	/* return status of password verification. */
	return ok;
}
