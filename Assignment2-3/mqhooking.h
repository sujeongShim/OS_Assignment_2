
#ifndef MQHOOKING_H
#define MQHOOKING_H

/* os_mq 명령어 */
#define OS_MQ_OPEN   0
#define OS_MQ_SEND   1
#define OS_MQ_RECV   2
#define OS_MQ_CLOSE  3

#define OS_MQ_NR 336
#define OS_MQ_CMD_COUNT 4

/* mqhooking 모듈에서 제공하는 통계 조회 함수 */
unsigned long long mq_get_count(int cmd);

#endif
