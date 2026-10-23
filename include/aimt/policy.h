#ifndef AIMT_POLICY_H
#define AIMT_POLICY_H

#include "aimt/app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AIMT_PROFILE_PERFORMANCE, AIMT_PROFILE_BALANCED, AIMT_PROFILE_SECURITY } aimt_profile;
typedef enum { AIMT_STAGE_PLAN, AIMT_STAGE_WORK, AIMT_STAGE_REVIEW } aimt_stage;
typedef enum { AIMT_LOCAL_NEW, AIMT_LOCAL_FAILED, AIMT_LOCAL_NO_MEMORY, AIMT_LOCAL_DONE } aimt_local_state;
typedef enum { AIMT_ROUTE_LOCAL, AIMT_ROUTE_CLOUD, AIMT_ROUTE_STOP } aimt_route;

typedef struct {
    aimt_profile profile;
    bool cloud_allowed;
    bool cloud_connected;
    bool cloud_coordinator;
    bool cloud_reviewer;
    bool cloud_fallback;
    unsigned local_attempt_limit;
    unsigned cloud_call_limit;
    unsigned cloud_calls_used;
} aimt_policy;

typedef struct {
    aimt_stage stage;
    aimt_local_state local_state;
    unsigned local_attempts;
    bool cloud_data_allowed;
} aimt_route_request;

typedef struct { aimt_route route; const char *reason; } aimt_route_decision;

aimt_policy aimt_policy_default(void);
aimt_result aimt_policy_set_profile(aimt_policy *policy, aimt_profile profile, bool safe_mode_ready);
/* Pure routing decision; does not authenticate, send data or invoke a model. */
aimt_route_decision aimt_policy_route(const aimt_policy *policy, const aimt_route_request *request);

#ifdef __cplusplus
}
#endif

#endif
