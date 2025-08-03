#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <pthread.h>
#include <zlib.h>
#include <unistd.h>

// 数据库连接信息
#define MASTER_HOST "localhost"
#define MASTER_USER "root"
#define MASTER_PASS "12345678"
#define SLAVE_HOST "localhost"
#define SLAVE_USER "root"
#define SLAVE_PASS "12345678"

// 条件变量和互斥锁，用于等待从库同步
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int sync_complete = 0;

// 计算CRC32哈希值
unsigned int crc32_hash(unsigned long long id) {
    return crc32(0, (const Bytef*)&id, sizeof(id));
}

// 确定数据库和表名
void get_db_table(unsigned long long id, char* db_name, char* table_name) {
    unsigned int hash = crc32_hash(id);
    unsigned int mod = hash % 256;
    
    // 确定数据库
    if (mod >= 0 && mod <= 15) {
        sprintf(db_name, "user_%d", mod / 8);
    } else {
        // 处理其他范围，这里简化处理
        sprintf(db_name, "user_%d", mod / 8);
    }
    
    // 确定表名
    sprintf(table_name, "user_info_%d", mod);
}

// 初始化数据库连接
MYSQL* init_connection(const char* host, const char* user, const char* pass, const char* db) {
    MYSQL* conn = mysql_init(NULL);
    if (!conn) {
        fprintf(stderr, "mysql_init failed\n");
        return NULL;
    }
    
    if (!mysql_real_connect(conn, host, user, pass, db, 0, NULL, 0)) {
        fprintf(stderr, "mysql_real_connect failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }
    
    return conn;
}

// 创建数据库和表（如果不存在）
void create_db_tables(MYSQL* conn) {
    char query[1024];
    
    // 创建数据库
    for (int i = 0; i < 32; i++) {  // 256/8=32个库
        sprintf(query, "CREATE DATABASE IF NOT EXISTS user_%d", i);
        if (mysql_query(conn, query)) {
            fprintf(stderr, "Failed to create database: %s\n", mysql_error(conn));
        }
    }
    
    // 创建表
    for (int i = 0; i < 32; i++) {
        sprintf(query, "USE user_%d", i);
        if (mysql_query(conn, query)) {
            fprintf(stderr, "Failed to use database: %s\n", mysql_error(conn));
            continue;
        }
        
        // 为每个库创建8个表
        for (int j = i*8; j < (i+1)*8 && j < 256; j++) {
            sprintf(query, 
                "CREATE TABLE IF NOT EXISTS user_info_%d ("
                "id BIGINT PRIMARY KEY,"
                "name VARCHAR(50) NOT NULL,"
                "email VARCHAR(100) NOT NULL UNIQUE"
                ")", j);
            
            if (mysql_query(conn, query)) {
                fprintf(stderr, "Failed to create table: %s\n", mysql_error(conn));
            }
        }
    }
}

// 插入用户到主库
int insert_user(MYSQL* master_conn, unsigned long long id) {
    char db_name[50], table_name[50];
    char query[1024];
    char name[50], email[100];
    
    // 生成用户名和邮箱
    sprintf(name, "user_%llu", id);
    sprintf(email, "user_%llu@example.com", id);
    
    // 确定数据库和表
    get_db_table(id, db_name, table_name);
    
    // 切换到对应的数据库
    sprintf(query, "USE %s", db_name);
    if (mysql_query(master_conn, query)) {
        fprintf(stderr, "Failed to use database: %s\n", mysql_error(master_conn));
        return 0;
    }
    
    // 插入用户
    sprintf(query, 
        "INSERT INTO %s (id, name, email) VALUES (%llu, '%s', '%s')",
        table_name, id, name, email);
    
    if (mysql_query(master_conn, query)) {
        fprintf(stderr, "Failed to insert user %llu: %s\n", id, mysql_error(master_conn));
        return 0;
    }
    
    return 1;
}

// 检查从库是否已同步指定ID的数据
int check_slave_sync(MYSQL* slave_conn, unsigned long long id) {
    char db_name[50], table_name[50];
    char query[1024];
    MYSQL_RES* result;
    MYSQL_ROW row;
    int count = 0;
    
    // 确定数据库和表
    get_db_table(id, db_name, table_name);
    
    // 切换到对应的数据库
    sprintf(query, "USE %s", db_name);
    if (mysql_query(slave_conn, query)) {
        fprintf(stderr, "Failed to use database: %s\n", mysql_error(slave_conn));
        return 0;
    }
    
    // 检查记录是否存在
    sprintf(query, "SELECT COUNT(*) FROM %s WHERE id = %llu", table_name, id);
    if (mysql_query(slave_conn, query)) {
        fprintf(stderr, "Failed to check sync: %s\n", mysql_error(slave_conn));
        return 0;
    }
    
    result = mysql_store_result(slave_conn);
    if (result) {
        row = mysql_fetch_row(result);
        if (row) {
            count = atoi(row[0]);
        }
        mysql_free_result(result);
    }
    
    return count > 0;
}

// 等待从库同步完成的线程函数
void* wait_for_sync(void* arg) {
    MYSQL* slave_conn = (MYSQL*)arg;
    unsigned long long last_id = 100000000 + 999;  // 最后一个用户ID
    int sync_done = 0;
    
    while (!sync_done) {
        // 检查最后一条记录是否已同步
        sync_done = check_slave_sync(slave_conn, last_id);
        
        if (!sync_done) {
            sleep(1);  // 等待1秒后重试
        }
    }
    
    // 通知主线程同步完成
    pthread_mutex_lock(&mutex);
    sync_complete = 1;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    
    return NULL;
}

// 从从库读取所有用户
void read_all_users(MYSQL* slave_conn) {
    char db_name[50], table_name[50];
    char query[1024];
    MYSQL_RES* result;
    MYSQL_ROW row;
    int total = 0;
    
    printf("\nReading all users from slave database...\n");
    
    // 遍历所有可能的表
    for (int mod = 0; mod < 256; mod++) {
        // 确定数据库和表
        sprintf(db_name, "user_%d", mod / 8);
        sprintf(table_name, "user_info_%d", mod);
        
        // 切换到对应的数据库
        sprintf(query, "USE %s", db_name);
        if (mysql_query(slave_conn, query)) {
            continue;  // 数据库可能不存在，跳过
        }
        
        // 读取表中的所有用户
        sprintf(query, "SELECT id, name, email FROM %s", table_name);
        if (mysql_query(slave_conn, query)) {
            continue;  // 表可能不存在，跳过
        }
        
        result = mysql_store_result(slave_conn);
        if (result) {
            while ((row = mysql_fetch_row(result))) {
                printf("ID: %s, Name: %s, Email: %s\n", row[0], row[1], row[2]);
                total++;
            }
            mysql_free_result(result);
        }
    }
    
    printf("\nTotal users read: %d\n", total);
}
