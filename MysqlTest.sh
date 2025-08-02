#!/bin/bash

# MySQL 配置信息，请根据实际环境修改
MYSQL_HOST="localhost"
MYSQL_USER="root"
MYSQL_PASS="12345678"
CHARSET="utf8mb4"
COLLATION="utf8mb4_general_ci" #设置格式为utf8mb4，支持所有Unicode字符

# 检查MySQL客户端是否安装
if ! command -v mysql &> /dev/null; then
    echo "错误：未安装MySQL客户端，请先安装。"
    exit 1
fi

# 测试MySQL连接（这俩是必要的验证）
echo "测试MySQL连接..."
if ! mysql -h "$MYSQL_HOST" -u "$MYSQL_USER" -p"$MYSQL_PASS" -e "SELECT 1" &> /dev/null; then
    echo "错误：无法连接到MySQL服务器，请检查配置。"
    exit 1
fi

# 创建数据库和表的函数
create_databases_and_tables() {
    # 创建8个数据库：user_0 到 user_7
    for db_index in {0..7}; do
        db_name="user_$db_index"
        echo "正在创建数据库: $db_name"
        
        # 创建数据库
        mysql -h "$MYSQL_HOST" -u "$MYSQL_USER" -p"$MYSQL_PASS" -e "
            CREATE DATABASE IF NOT EXISTS $db_name 
            CHARACTER SET $CHARSET 
            COLLATE $COLLATION;
        "
        
        # 计算当前数据库中表的起始索引
        start_table_index=$((db_index * 16))
        end_table_index=$((start_table_index + 15))
        
        # 在当前数据库中创建16个表
        for ((table_index=start_table_index; table_index<=end_table_index; table_index++)); do
            table_name="user_info_$table_index"
            echo "  正在创建表: $db_name.$table_name"
            
            # 创建表结构和索引
            mysql -h "$MYSQL_HOST" -u "$MYSQL_USER" -p"$MYSQL_PASS" $db_name -e "
                CREATE TABLE IF NOT EXISTS $table_name (
                    id INT UNSIGNED NOT NULL AUTO_INCREMENT,
                    name VARCHAR(50) NOT NULL,
                    phone VARCHAR(20) NOT NULL,
                    age TINYINT UNSIGNED,
                    school VARCHAR(100),
                    gender ENUM('male', 'female', 'other'),
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    
                    -- 主键
                    PRIMARY KEY (id),
                    
                    -- 唯一索引
                    UNIQUE INDEX idx_id (id),
                    UNIQUE INDEX idx_name (name),
                    
                    -- 组合索引
                    INDEX idx_id_name_phone (id, name, phone)
                ) ENGINE=InnoDB DEFAULT CHARSET=$CHARSET COLLATE=$COLLATION;
            "
        done
    done
}

# 执行创建操作
create_databases_and_tables

echo "所有数据库和表创建完成！"

# 验证创建结果（验证数据库创建数量）
echo "验证创建结果..."
total_dbs=$(mysql -h "$MYSQL_HOST" -u "$MYSQL_USER" -p"$MYSQL_PASS" -e "SHOW DATABASES LIKE 'user_%'" | wc -l)
if [ $((total_dbs - 1)) -eq 8 ]; then
    echo "成功创建了8个数据库"
else
    echo "警告：数据库数量不正确，实际创建了 $((total_dbs - 1)) 个"
fi

# 显示最后一个库的表数量（验证表的数量）
last_db="user_7"
total_tables=$(mysql -h "$MYSQL_HOST" -u "$MYSQL_USER" -p"$MYSQL_PASS" -e "USE $last_db; SHOW TABLES LIKE 'user_info_%'" | wc -l)
if [ $((total_tables - 1)) -eq 16 ]; then
    echo "最后一个数据库 $last_db 成功创建了16个表"
else
    echo "警告：$last_db 表数量不正确，实际创建了 $((total_tables - 1)) 个"
fi

