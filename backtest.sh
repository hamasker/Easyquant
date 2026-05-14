#!/bin/bash

# 获取脚本所在目录作为 BASE_PATH
BASE_PATH=$(cd "$(dirname "$0")"; pwd)
config_num=$1

# 切换到 coinsimulator 目录
cd "$BASE_PATH/ext/coinsimulator" || { echo "无法进入 coinsimulator 目录"; exit 1; }

# 设置程序和配置路径
EXEC="./bin/SimTrade"
CONFIG="$BASE_PATH/config/taking_all_multi$config_num.json"
LOG_DIR=$(jq -r '.Server.Log.path' "$CONFIG")
DATE=$(date +%Y%m%d)
LOG_FILE="${LOG_DIR}.$DATE"

# 运行程序
"$EXEC" -f "$CONFIG"

# 检查日志是否生成
if [ ! -f "$LOG_FILE" ]; then
    echo "日志文件未找到: $LOG_FILE"
    exit 1
fi

# 提取 JSON 参数（使用 jq）
assets_len=$(jq '.Strategy.digital_currencies | length' "$CONFIG")
if [ "$assets_len" -eq 0 ]; then
    asset_tag="base"
elif [ "$assets_len" -eq 1 ]; then
    asset_tag=$(jq -r '.Strategy.digital_currencies[0]' "$CONFIG")
else
    asset_tag="all"
fi

flag_ib=$(jq -r '.Strategy.flag_ib' "$CONFIG")
if [ "$flag_ib" = "false" ]; then
    ib_tag=""
else
    ib_tag="Ib10"
fi

flag_abnormal=$(jq -r '.Strategy.flag_abnormal' "$CONFIG")
abnormal_thre=$(jq -r '.Strategy.abnormal_thre' "$CONFIG")
if [ "$flag_abnormal" = "false" ]; then
    abnormal_tag=""
else
    abnormal_tag="Ab${abnormal_thre}"
fi

flag_flex=$(jq -r '.Strategy.flexible_adjust' "$CONFIG")
if [ "$flag_flex" = "false" ]; then
    flex_tag=""
else
    flex_tag="F"
fi

flag_flex_vt=$(jq -r '.Strategy.flexible_vol_thre' "$CONFIG")
if [ "$flag_flex_vt" = "false" ]; then
    flex_vt_tag=""
else
    flex_vt_tag="Fvt13"
fi

flag_slope=$(jq -r '.Strategy.flag_slope' "$CONFIG")
factor_min=$(jq -r '.Strategy.factor_min' "$CONFIG")
factor_max=$(jq -r '.Strategy.factor_max' "$CONFIG")
if [ "$flag_slope" = "false" ]; then
    slope_tag=""
else
    slope_tag="S${factor_min}-${factor_max}"
fi

vol_method=$(jq -r '.Strategy.volatility_method' "$CONFIG")
vol_multi=$(jq -r '.Strategy.vol_multi' "$CONFIG")
vol_multi_base=$(jq -r '.Strategy.vol_multi_base' "$CONFIG")
case "$vol_method" in
    "fp_max_min_shared")
        method_tag="mms${vol_multi}-${vol_multi_base}"
        ;;
    "fp_diff_shared")
        method_tag="diffs${vol_multi}-${vol_multi_base}"
        ;;
    "fp_max_min")
        method_tag="mm${vol_multi}-${vol_multi_base}"
        ;;
    "fp_diff")
        method_tag="diff${vol_multi}-${vol_multi_base}"
        ;;
    *)
        method_tag="unknown"
        ;;
esac

neg_thre=$(jq -r '.Strategy.negative_threshold' "$CONFIG")

# 检查日志文件是否存在
if [ ! -f "$LOG_FILE" ]; then
    echo "日志文件未找到：$LOG_FILE"
    exit 1
fi

# 提取日志中最后一条含有 "pnl static hedge:" 的行
last_pnl_line=$(grep "pnl static hedge:" "$LOG_FILE" | tail -n 1)

if [ -z "$last_pnl_line" ]; then
    echo "未在日志中找到 'pnl static hedge:' 行"
    exit 1
fi

# 获取最后一条包含 time: 的行
last_time_line=$(grep "*time: 20" "$LOG_FILE" | tail -n 1)

if [ -z "$last_time_line" ]; then
    echo "未在日志中找到 'time:' 行"
    exit 1
fi

# 提取 time: 后面的时间字符串
# 例如：如果这一行是 ... time: 20250403 09:45:40 ...
log_time=$(echo "$last_time_line" | sed -En 's/.*time: ([0-9]{4}-?[0-9]{2}-?[0-9]{2} [0-9:]{8}).*/\1/p')

# 获取时间中的日期和时分秒
log_date=${log_time%% *}       # 20250403
log_hms=${log_time#* }         # 09:45:40

# 生成“当日23:50:00”的时间字符串
threshold_time="${log_date} 23:50:00"

# 转为时间戳（秒）
log_timestamp=$(date -d "$log_time" +%s)
threshold_timestamp=$(date -d "$threshold_time" +%s)

# 判断
if (( log_timestamp > threshold_timestamp )); then
    valid_tag="valid"
else
    valid_tag="invalid"
fi

# 提取 pnl static hedge: 后面的数值
pnl_val=$(echo "$last_pnl_line" | sed -n 's/.*pnl static hedge:[[:space:]]*\([0-9.eE+-]*\).*/\1/p')
pnl_tag=$(printf "%.0f" "$pnl_val")

# 提取 trade usd: 后面的数值
vol_val=$(echo "$last_pnl_line" | sed -n 's/.*trade usd:[[:space:]]*\([0-9.eE+-]*\).*/\1/p')
volume_tag=$(printf "%.0f" "$vol_val")

# 拼接最终日志新文件名
LOG_FILE_NEW="${LOG_FILE}.${asset_tag}.C1${ib_tag}${method_tag}_neg${neg_thre_pnl}${pnl_tag}_v${volume_tag}_${valid_tag}"

FILE=$(basename "$LOG_FILE_NEW")
BEGIN_TIME=$(jq -r '.Quote.backtest.begin_time' "$CONFIG")
DATE2=${BEGIN_TIME%%.*}

LOG_DIR_PATH=$(dirname "$LOG_DIR")
mkdir -p "${LOG_DIR_PATH}/${DATE}"
# 重命名
mv "$LOG_FILE" "${LOG_DIR_PATH}/${DATE}/${FILE}"
echo "日志文件重命名为：$LOG_FILE_NEW"