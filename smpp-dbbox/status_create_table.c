#include<mysql/mysql.h>
#include<stdio.h>
#include<time.h>
#include<string.h>
#include<stdlib.h>
#include<mysql/mysqld_error.h>
#include<unistd.h>

#define SW_VERSION 1.2
#define NO_OF_COLS 6

char *table[] =  	{	"SNo int primary key auto_increment,",
						"SMSCId varchar(40) ,"				,
						"SMSCMsgId varchar(68),"			,
						"DestinationAddr varchar(20),"			,
						"DeliveredStat int ,"				,
						"DeliverTime datetime"				
						
					};

char *fields[] =  	{	"SNo int",
						"SMSCId"				,
						"SMSCMsgId"				,
						"DestinationAddr"		,
						"DeliveredStat"			,
						"DeliverTime"									
					};



char *host_name=NULL;
char *user_name=NULL;
char *password=NULL;
char *db_name=NULL;
char *socket_name=NULL;
char *clientid=NULL;
char *arg_table_name=NULL;

unsigned int opt_port_num=0;
unsigned int opt_flags=0;

char status_table_name[50];
char table_name[50];
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
		//printf("4. -c : Client- Name\n");
		printf("4. -u : Username\n");
		printf("5. -p : Password\n");
		printf("6. -s : Mysql Socket path\n");
		printf("7. -o : Mysql Socket port\n");
		printf("8. -f : use all defaults values\n");
		printf("9. -n : table name\n");
		printf("10. -m : machine name\n");
		return 0;
		
	}
	
	
	while( (opt = getopt(argc,argv,"h::v::u:p:d:m:s:o:n:f::")) !=EOF)
	{
		switch(opt)
		{
			
			case 'n':
			{
				valid_option = 1;
				arg_table_name = optarg;
				printf("\n-n option : table name is :%s",arg_table_name);
				break;
			}
			case 'h':
			{
				printf("\nOPTIONS ARE\n");
				printf("1. -h : help\n");
				printf("2. -v : Software version\n");
				printf("3. -d : Select Database Name\n");
				//printf("4. -c : Client- Name\n");
				printf("4. -u : Username\n");
				printf("5. -p : Password\n");
				printf("6. -s : Mysql Socket path\n");
				printf("7. -o : Mysql Socket port\n");
				printf("8. -f : use all defaults values\n");
				printf("9. -n : table name\n");
				printf("10. -m : machine name\n");
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
				
			case 'm':
			{
				valid_option = 1;
				host_name = optarg;
				printf("\n-m option : host machine is :%s",host_name);
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
				
				printf("Pls specify valid options\n");
				printf("\nOPTIONS ARE\n");
				printf("1. -h : help\n");
				printf("2. -v : Software version\n");
				printf("3. -d : Select Database Name\n");
				//printf("4. -c : Client- Name\n");
				printf("4. -u : Username\n");
				printf("5. -p : Password\n");
				printf("6. -s : Mysql Socket path\n");
				printf("7. -o : Mysql Socket port\n");
				printf("8. -f : use all defaults values\n");
				printf("9. -n : table name\n");
				printf("10. -m : machine name\n");
				exit(0);
			}
			
		}//switch 
	}
	
	if(!valid_option)
	{
		printf("\nERROR : Specify valid command line argument\n");
		exit(0);
	}
	if(arg_table_name == NULL)
	{
		printf("\nPlease specify the table name with -n option\n");
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
	
	
	
	
	/* create status table datewise-----*/
	struct tm *ptr;
	time_t tm;
	char table_name[20];
	char temp[25];
	char temp1[25],temp2[25],temp3[25];
	char ans;
	int month[12]={31,28,31,30,31,30,31,31,30,31,30,30};
	int j,k,number,number1,number3,day;
	char nu[5];char mon[3],date[3];
	
	tm=time(0);
	ptr = localtime(&tm);
	strftime(mon,20,"%m",ptr);
	sscanf(mon,"%d",&number);

	/*if the tables required for current month*/
	/*
	printf("Do YOU want to create tables for current month y/n:");
	scanf("%c",&ans);
	printf("ans %c",ans);
	*/
	ans = 'y';
	if(ans=='Y'||ans=='y')
	{

		strftime(date,20,"%d",ptr);
		sscanf(date,"%d",&day);
		k=month[number-1];
		strftime(temp1,20,"_%m",ptr);
		//sprintf(temp1,"%d",number);
		strftime(temp2,20,"%m",ptr);
		//sprintf(temp1,"%d",number);
		/*for leap year--------------*/
		sscanf(temp2,"%d",&number1);
    	//printf("NUMBER++++    %d",number1);
		if(number1==2)
		{
    		
			//strftime(temp2,20,"_%Y",ptr);
			//sscanf(temp2,"%d",&number3);
    		strftime(temp3,20,"%Y",ptr);
			number3 = atoi(temp3);
			if(((number3%400)==0)||(((number3%100)!=0)&&((number3%4)==0)))
    		{
        		   printf("FEB HAS 29 days-----1\n");
        		  k=29;
			}
		}
		
       	 for(i=day;i<=k;i++)
       	 {
			
			strcpy(table_name,arg_table_name);
			
		/*if(number<=9)
		     strcat(table_name,"_0");
		else 
		      strcat(table_name,"_");*/
		 strcat(table_name,temp1);
			//strcat(table_name,"_");
			if(i<=9)
			{
			
				strcat(table_name,"_0");			
			}
			else 
				strcat(table_name,"_");
			
			j=sprintf(nu,"%d",i);
			strcat(table_name,nu);
			
			strcpy(status_table_name,table_name); 
			strcpy(query,"CREATE TABLE ");
			strcat(query,status_table_name);
			//strcat(query,table_name);
			strcat(query," ( ");
			for(j = 0 ; j < NO_OF_COLS ; j++)
				strcat(query,table[j]);
			strcat(query," ); ");

			//printf("\nquerry is :%s\n",query);
			ret = 0;
			ret = mysql_query(conn,query);
			if(ret != 0)
			{
				error_code = mysql_errno(conn);
				if(error_code == ER_TABLE_EXISTS_ERROR)
				{
					printf("\nMYSQL TABLE %s already exixts \n",status_table_name);
				}
				else
				{
					printf("\nMYSQL TABLE CREATION FAILED :%d :%s\n",mysql_errno(conn),mysql_error(conn));
					mysql_close(conn);
			        return -1;
		        }

			}
			else
			{
				printf("MYSQL TABLE CREATED %s\n",status_table_name);
				/*creating composite index on SMSCId and SMSCMsgid*/
				strcpy(query,"CREATE INDEX ");
				strcat(query,status_table_name);
				strcat(query,"_ind");
				strcat(query, " ON ");
				strcat(query,status_table_name);
				strcat(query,"(");
				strcat(query,fields[2]);
				strcat(query,",");
				strcat(query,fields[3]);
				strcat(query,");");
				mysql_query(conn,query);
				
    			
			}
			memset(table_name,'\0',20);
	      }//for
	 }//ans yes
	if(number == 12)
		number = 0;
	k=month[number];
	//printf("K is ....%d\n ",k);
	number++;
		strftime(temp,20,"_%m",ptr);
		strftime(temp2,20,"%m",ptr);
	sprintf(temp2,"%d",number);
	
	//strftime(temp2,20,"%m",ptr);
	//sprintf(temp1,"%d",number);
	sscanf(temp2,"%d",&number1);

	if(number==2)
	{
		strftime(temp3,20,"%Y",ptr);
		//sscanf(temp2,"%d",&number3);
		number3 = atoi (temp3);
		if((number3%400==0)||((number3%100!=0)&&(number3%4==0)))
		{
			printf("FEB HAS 29 days-----\n");
			k=29;
		}
	}
              
    for(i=1;i<=k;i++)
    {
		
		strcpy(table_name,arg_table_name);
		
		if(number<=9)
		     strcat(table_name,"_0");
		else 
		      strcat(table_name,"_");
		
		 strcat(table_name,temp2);
		 if(i<=9)
			{
			
			strcat(table_name,"_0");
			
			}
			else strcat(table_name,"_");
		 
		 j=sprintf(nu,"%d",i);
		 strcat(table_name,nu);
		 //printf("table name %s \n",table_name);  
		strcpy(status_table_name,table_name); 
		 strcpy(query,"CREATE TABLE ");
		strcat(query,status_table_name);
		 //strcat(query,table_name);
		strcat(query," ( ");
		for(j = 0 ; j < NO_OF_COLS ; j++)
			strcat(query,table[j]);
		strcat(query," ); ");

		//printf("\nquerry is :%s\n",query);
		ret = 0;
		ret = mysql_query(conn,query);
		if(ret != 0)
		{
			error_code = mysql_errno(conn);
			if(error_code == ER_TABLE_EXISTS_ERROR)
			{
				printf("\nMYSQL TABLE %s already exixts \n",status_table_name);
			}
			else
			{
				mysql_close(conn);
				printf("\nMYSQL TABLE CREATION FAILED\n");
				return -1;
			}

		}
		else
		{
			printf("MYSQL TABLE CREATED %s\n",status_table_name);
			/*creating composite index on SMSCId and SMSCMsgid*/
			strcpy(query,"CREATE INDEX ");
			strcat(query,status_table_name);
			strcat(query,"_ind");
			strcat(query, " ON ");
			strcat(query,status_table_name);
			strcat(query,"(");
			strcat(query,fields[2]);
			strcat(query,",");
			strcat(query,fields[3]);
			strcat(query,");");
			mysql_query(conn,query);			
				
        }
		memset(table_name,'\0',20);

	}//for

	mysql_close(conn);
	return 0;
}
