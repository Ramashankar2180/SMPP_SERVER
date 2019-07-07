#include <cstdlib>
#include <stdio.h>
#include <mysql.h>
#include <iostream>
#include <bson.h>
#include <mongoc.h>
#include <vector>

using namespace std;

char *replaceWord(const char *s, const char *oldW, const char *newW) {
	char *result;
	int i, cnt = 0;
	int newWlen = strlen(newW);
	int oldWlen = strlen(oldW);

	for (i = 0; s[i] != '\0'; i++) {
		if (strstr(&s[i], oldW) == &s[i]) {
			cnt++;
			i += oldWlen - 1;
		}
	}

	result = (char *) malloc(i + cnt * (newWlen - oldWlen) + 1);

	i = 0;
	while (*s) {
		if (strstr(s, oldW) == s) {
			strcpy(&result[i], newW);
			i += newWlen;
			s += oldWlen;
		} else
			result[i++] = *s++;
	}

	result[i] = '\0';
	return result;
}

void checker(MYSQL *conn, mongoc_collection_t *collection) {
	MYSQL_ROW row;
	vector<string> columnname;

	if (conn == NULL) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}

	if (mysql_query(conn, "SELECT ESMEAccName, ESMEAccMsgId, SMSCId, SMSCMsgId, SourceAddr, DestinationAddr, RecvTime, SubmittedStat, SubmitTime, DeliveredStat, DeliverTime, RetryValue, UDH, Content,Client_IP,data_coding, esm_class,more_messages_to_send FROM smpp_esme_table where dnd_flag=0")) {
		fprintf(stderr, "%s\n", mysql_error(conn));
	}

	MYSQL_RES *result = mysql_store_result(conn);

	if (result == NULL) {
		fprintf(stderr, "%s\n", mysql_error(conn));
	}

	int num_fields = mysql_num_fields(result);

	MYSQL_FIELD *field;
	for (unsigned int i = 0; (field = mysql_fetch_field(result)); i++) {
		columnname.push_back(field->name);
	}

	while ((row = mysql_fetch_row(result))) {
		string val = "";
		string query = "INSERT INTO process_table (";
		for (int i = 0; i < num_fields; i++) {
			if (row[i]) {
				query += columnname[i];
				query += ",";

				if (columnname[i] == "Content") {
					row[i] = replaceWord(row[i], "'", "\\'");
				}
				val += "'";
				val += row[i];
				val += "',";
			}
		}

		bson_error_t error;
		bson_t *doc;
		int64_t count;
		char data[500];
		sprintf(data, "{\"phoneNum\" : \"%s\"}", row[5]);
		doc = bson_new_from_json((const uint8_t *) data, -1, &error);
		count = mongoc_collection_count(collection, MONGOC_QUERY_NONE, doc, 0, 0, NULL, &error);
		if (count < 1) {
			//  cout << "non dnd" << endl;
		} else {
			query += "dnd,";
			val += "'1',";
		}

		query = query.substr(0, query.length() - 1);
		val = val.substr(0, val.length() - 1);
		query += ") values (";
		val += ") ";
		query += val;
		if (mysql_query(conn, query.c_str())) {
			fprintf(stderr, "%s\n", mysql_error(conn));
		} else {
			query = "update smpp_esme_table set dnd_flag=1 where ESMEAccMsgId='";
			query += row[1];
			query += "'";
			if (mysql_query(conn, query.c_str())) {
				fprintf(stderr, "%s\n", mysql_error(conn));
			}
		}
		bson_destroy(doc);

	}

	mysql_free_result(result);
}

int main(int argc, char** argv) {
	MYSQL *conn = mysql_init(NULL);
	if (conn == NULL) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}

	/* Connect to database */
	if (!mysql_real_connect(conn, "10.10.1.44", "root", "fonada@123", "Fonada_Smpp", 0, NULL, 0)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	} else
		cout << "DB connected" << endl;

	mongoc_client_t *client;
	mongoc_collection_t *collection;
	mongoc_init();
	client = mongoc_client_new("mongodb://10.10.1.41:27017");
	collection = mongoc_client_get_collection(client, "fonada", "dnd");
	while (1) {
		checker(conn, collection);
		sleep(2);
	}

	mongoc_collection_destroy(collection);
	mongoc_client_destroy(client);
	mongoc_cleanup();

	mysql_close(conn);

	return 0;
}
