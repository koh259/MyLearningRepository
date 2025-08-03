#include "Mysql.h"

int main() {
    MYSQL* master_conn;
    MYSQL* slave_conn;
    pthread_t sync_thread;
    unsigned long long start_id = 100000000;
    int num_users = 1000;
    
    // 初始化主库连接
    master_conn = init_connection(MASTER_HOST, MASTER_USER, MASTER_PASS, NULL);
    if (!master_conn) {
        return 1;
    }
    
    // 初始化从库连接
    slave_conn = init_connection(SLAVE_HOST, SLAVE_USER, SLAVE_PASS, NULL);
    if (!slave_conn) {
        mysql_close(master_conn);
        return 1;
    }
    
    // 创建数据库和表
    printf("Creating databases and tables if not exists...\n");
    create_db_tables(master_conn);
    
    // 插入用户
    printf("Inserting %d users into master database...\n", num_users);
    for (int i = 0; i < num_users; i++) {
        unsigned long long user_id = start_id + i;
        if (!insert_user(master_conn, user_id)) {
            printf("Failed to insert user %llu\n", user_id);
        }
    }
    
    // 启动同步检测线程
    printf("Waiting for slave database to sync...\n");
    if (pthread_create(&sync_thread, NULL, wait_for_sync, slave_conn) != 0) {
        fprintf(stderr, "Failed to create sync thread\n");
        mysql_close(master_conn);
        mysql_close(slave_conn);
        return 1;
    }
    
    // 等待同步完成
    pthread_mutex_lock(&mutex);
    while (!sync_complete) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    
    printf("Slave database sync complete!\n");
    
    // 读取所有用户
    read_all_users(slave_conn);
    
    // 清理资源
    pthread_join(sync_thread, NULL);
    mysql_close(master_conn);
    mysql_close(slave_conn);
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
    
    return 0;
}

