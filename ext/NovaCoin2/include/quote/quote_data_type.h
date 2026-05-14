#pragma once

#include "api/nova_api_data_type.h"
#include "base/base_util.h"

BEGIN_NOVA_NAMESPACE(quote)

constexpr uint64_t LOGIN_TIME_OUT_NS = 30ll * 1000ll * 1000ll * 1000llu;

#define CONFIG_QUOTE "Quote"

#define COINFIG_IP ".ip"
#define COINFIG_PORT ".port"
#define COINFIG_QUERY_IP ".query_ip"
#define COINFIG_QUERY_PROT "query_port"
#define COINFIG_USER ".user"
#define COINFIG_FUND_ACCOUNT ".fund_account"
#define COINFIG_PASSWORD ".password"
#define COINFIG_PROTOCOL ".protocol"
#define COINFIG_INSTRUMENTS ".unstruments"
#define COINFIG_CPU_CORE ".cpu_core"
#define COINFIG_LOG_LEVEL ".log_level"
#define COINFIG_LOG_PATH ".log_path"
#define COINFIG_CLIENT_ID ".client_id"
#define COINFIG_ORDERS ".orders"
#define COINFIG_INTERFACE ".interface"
#define COINFIG_BROKER_ID ".broker_id"
#define COINFIG_ACCOUNT_ID ".account_id"
#define COINFIG_INVESTOR_ID ".investor_id"
#define COINFIG_KEY_FILE ".key_file"
#define COINFIG_CMD ".cmd"
#define COINFIG_TOPIC_ID ".topic_id"

#define COINFIG_UDP_IP ".idp_ip"
#define COINFIG_UDP_PORT ".udp_port"
#define COINFIG_LOCAL_PORT ".local_port"

#define COINFIG_TCP_IP ".tcp_ip"
#define COINFIG_TCP_PORT ".tcp_port"

#define COINFIG_SRC_IP ".src_ip"
#define COINFIG_SRC_PORT ".src_port"
#define COINFIG_DST_MAC ".dst_mac"

#define COINFIG_INTERVAL ".interval"

#define COINFIG_PRODINFO ".prodinfo"
#define COINFIG_MACADDR ".macaddr"
#define COINFIG_AUTH_CODE ".auth_code"
#define COINFIG_APP_ID ".app_id"
#define COINFIG_BUSY_WAIT ".busy_wait"

#define COINFIG_FRONT_ADDRESS ".front_address"
#define COINFIG_QUERY_FRONT_ADDRESS ".query_front_address"
#define COINFIG_PRIVATE_FRONT_ADDRESS ".prevate_front_address"
#define COINFIG_PARICIPANT_ID ".participant_id"
#define COINFIG_INE_PARICIPANT_ID ".ine_participant_id"

#define COINFIG_ENVIRONMENT ".environment"
#define COINFIG_LIB ".lib"

#define COINFIG_TAT ".tbt"
#define COINFIG_DEPTH_X ".depth_X"
#define COINFIG_TYPE ".type"
#define COINFIG_QUEUE ".queue"

#define COINFIG_CONFIG_FILE ".config_file"

#define COINFIG_CPU1 ".cpu1"
#define COINFIG_CPU2 ".cpu2"

#define COINFIG_MDQP_FILE ".mdqp_file"

#define COINFIG_UDP_RECV ".udp_recv"

#define COINFIG_CHANNEL ".channel"
#define COINFIG_LEVEL1 ".level1"
#define COINFIG_FILTER_SAME_TBT ".filter_same_tbt"
#define COINFIG_LEVEL1 ".level1"

#define COINFIG_MARK_PRICE_KLINE ".mark_price_kline"
#define COINFIG_PREMIUM_INDEX_KLINE ".premium_index_kline"
#define COINFIG_OPEM_INTEREST ".open_interest"
#define COINFIG_TOP_LONG_SHORT_ACCOUNT_RATIO ".top_long_short_account_ratio"
#define COINFIG_TOP_LONG_SHORT_POSITION_RATIO ".top_long_short_position_ratio"
#define COINFIG_GLOBAL_LONG_SHORT_ACCOUNT_RATIO                                \
  ".global_long_short_account_ratio"

END_NOVA_NAMESPACE(quote)
