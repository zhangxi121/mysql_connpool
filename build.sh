#!/bin/bash

# // "/usr/include/mysql/mysql.h"
# // root # ls /usr/include/mysql/  #==> xxx.h
# //
# // root # find / -name "libmysqlclient.so"  #==>  /usr/lib/x86_64-linux-gnu/libmysqlclient.so
# //
# // root # ls /usr/lib/x86_64-linux-gnu/libmysql*
# // /usr/lib/x86_64-linux-gnu/libmysqlclient.a   /usr/lib/x86_64-linux-gnu/libmysqlclient.so.20       /usr/lib/x86_64-linux-gnu/libmysqld.a
# // /usr/lib/x86_64-linux-gnu/libmysqlclient.so  /usr/lib/x86_64-linux-gnu/libmysqlclient.so.20.3.20  /usr/lib/x86_64-linux-gnu/libmysqlservices.a
# //
# // root # cd /usr/lib/x86_64-linux-gnu/
# // root @:/usr/lib/x86_64-linux-gnu# ls libmysql*
# // libmysqlclient.a  libmysqlclient.so  libmysqlclient.so.20  libmysqlclient.so.20.3.20  libmysqld.a  libmysqlservices.a
# //
# // 一定要安装 MariaDB-shared , 否则编译时候找不到库、报错,
# // [root@centos lib64]# rpm -qa|grep mariadb
# // [root@centos lib64]# yum list installed|grep mariadb
# // MariaDB-client.x86_64                 10.4.21-1.el7.centos           @mariadb   
# // MariaDB-common.x86_64                 10.4.21-1.el7.centos           @mariadb   
# // MariaDB-compat.x86_64                 10.4.21-1.el7.centos           @mariadb   
# // MariaDB-devel.x86_64                  10.4.21-1.el7.centos           @mariadb   
# // MariaDB-server.x86_64                 10.4.21-1.el7.centos           @mariadb   
# // MariaDB-shared.x86_64                 10.4.21-1.el7.centos           @mariadb   
# // galera-4.x86_64                       26.4.9-1.el7.centos            @mariadb  
# // 
#    
# 

exeprogram="test_connpool"
if [ -f $exeprogram ];then 
    rm $exeprogram
fi 
g++ *.cpp -g -o $exeprogram -L/usr/lib64/mysql -lmysqlclient -lpthread -std=c++14


