#include<mysql/mysql.h>
#include<stdio.h>
#include<time.h>
#include<string.h>
#include<stdlib.h>
#include<mysql/mysqld_error.h>
#include<unistd.h>

#define SW_VERSION 1.1
#define NO_OF_COLS 6

char *table[] =  	{	"SNo int primary key auto_increment,",
						"AccountName varchar(40) ,"			,
						"AccountMsgId varchar(40) ,"		,	
						"SMSCId varchar(40) ,"				,
						"SMSCMsgId varchar(40),"			,
						"DatetimeSubmit datetime"
					};



char *host_name=NULL;
char *user_name=NULL;
char *password=NULL;
char *db_name=NULL;
char *socket_name=NULL;
char *clientid=NULL;
char *table_name=NULL;


unsigned int opt_port_num=0;
unsigned int opt_flags=0;





int main(int argc, char **argv)
{

	
	
	MYSQL *conn;
	char query[500];
	
	int ret;
	int i = 0;
	int error_code;
	int opt;
	int valid_option = 0 ;
	

	
	
	//printf("\nargs are : %d\n",argc);
	
	printf("\n-----------------App_Master Software Version is : %0.2f---------------------\n",SW_VERSION);
	
	if(argc == 1)
	{
		printf("Pls specify valid options\n");
		printf("\nOPTIONS ARE\n");
		printf("1. -h : help\n");
		printf("2. -v : Software version\n");
		printf("3. -d : Select Database Name\n");
		printf("4. -c : Client- Name\n");
		printf("5. -u : Username\n");
		printf("6. -p : Password\n");
		printf("7. -s : Mysql Socket path\n");
		printf("8. -o : Mysql Socket port\n");
		printf("9. -f : use all defaults values\n");
		printf("10. -n : table name\n");
		return 0;
		
	}
	
	
	while( (opt = getopt(argc,argv,"h::v::u:p:d:c:s:o:n:f::")) !=EOF)
	{
		switch(opt)
		{
			
			case 'n':
			{
				valid_option = 1;
				table_name = optarg;
				printf("\n-n option : table name is :%s",table_name);
				break;
			}
			case 'h':
			{
				printf("\nOPTIONS ARE\n");
				printf("1. -h : help\n");
				printf("2. -v : Software version\n");
				printf("3. -d : Select Database Name\n");
				printf("4. -c : Client- Name\n");
				printf("5. -u : Username\n");
				printf("6. -p : Password\n");
				printf("7. -s : Mysql Socket path\n");
				printf("8. -o : Mysql Socket port\n");
				printf("9. -f : use all defaults values\n");
				exit(0);
				break;
			}
			case 'v':
			{
				printf("\nSoftware Version is %0.2f\n",SW_VERSION);
				exit(0);
				break;
			}
			
			case 'u':
			{
				valid_option = 1;
				user_name = optarg;
				printf("\n-u option : username is :%s",user_name);
				break;
			}
			
			case 'p':
			{
				valid_option = 1;
				password = optarg;
				printf("\n-p option : password is :%s",password);
				break;
			}
			
			case 'd':
			{
				valid_option = 1;
				db_name = optarg;
				printf("\n-d option : selected database is :%s",db_name);
				break;
			}
			
			case 'c':
			{
				valid_option = 1;
				clientid = optarg;
				printf("\n-c option : client id is :%s",clientid);
				break;
			}
			
			case 's':
			{
				valid_option = 1;
				socket_name = optarg;
				printf("\n-s option : socket path is :%s",socket_name);
				break;
			}
			case 'o':
			{
				valid_option = 1;
				opt_port_num = atoi(optarg);
				printf("\n-o option : port is :%d",opt_port_num);
				break;
			}
			
			case 'f':
			{
				valid_option = 1;
				printf("\n-f option: using default values for connection");
				break;
			}
			default :
			{
				printf("\nOPTIONS ARE\n");
				printf("1. -h : help\n");
				printf("2. -v : Software version\n");
				printf("3. -d : Select Database Name\n");
				printf("4. -c : Client- Name\n");
				printf("5. -u : Username\n");
				printf("6. -p : Password\n");
				printf("7. -s : Mysql Socket path\n");
				printf("8. -o : Mysql Socket port\n");
				printf("9. -f : use all defaults values\n");
				exit(0);
			}
			
		}//switch 
	}
	
	if(!valid_option)
	{
		printf("\nERROR : Specify valid command line argument\n");
		exit(0);
	}
	
	
	

	conn = mysql_init(NULL);
	if(conn == NULL)
	{
		printf("\nMYSQL INIT FAILED : %d  :%s\n",mysql_errno(conn),mysql_error(conn));
		mysql_close(conn);
		return -1;
	}
	if(mysql_real_connect(conn, 
					   host_name,
                       user_name,
                       password,
                       db_name, 
                       opt_port_num, 
					   socket_name, 
					   0) == NULL)
	
	{
		printf("\nMYSQL CONN FAILED   :%d %s\n",mysql_errno(conn),mysql_error(conn));
		
		mysql_close(conn);
		return -1;
	}				   
	printf("\nMYSQL: server version %s, client version %s.\n", mysql_get_server_info(conn), mysql_get_client_info());
	
	/* TABLE CREATION*/
	
	
	strcpy(query,"CREATE TABLE ");
	strcat(query,table_name);
	strcat(query," ( ");
	for(i = 0 ; i < NO_OF_COLS ; i++)
		strcat(query,table[i]);
	strcat(query," ); ");
	
	printf("\nquerry is :%s\n",query);
	
	ret = mysql_query(conn,query);
	if(ret != 0)
	{
		error_code = mysql_errno(conn);
		if(error_code == ER_TABLE_EXISTS_ERROR)
		{
			printf("\nMYSQL TABLE %s already exists \n",table_name);
		}
		else
		{
			printf("\nMYSQL TABLE CREATION FAILED %s : %d\n",mysql_error(conn),error_code);
			mysql_close(conn);
			return -1;
		}
	}
	else
		printf("\nMYSQL TABLE CREATED %s\n",table_name);
	
		
	mysql_close(conn);
	return 0;
}
