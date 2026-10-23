#include "aimt/policy.h"

aimt_policy aimt_policy_default(void)
{
    return (aimt_policy){.profile=AIMT_PROFILE_BALANCED, .cloud_coordinator=true,
        .cloud_reviewer=true, .cloud_fallback=true, .local_attempt_limit=2, .cloud_call_limit=20};
}

aimt_result aimt_policy_set_profile(aimt_policy *policy, aimt_profile profile, bool safe_mode_ready)
{
    if (policy == NULL || profile < AIMT_PROFILE_PERFORMANCE || profile > AIMT_PROFILE_SECURITY) return AIMT_INVALID;
    if (profile == AIMT_PROFILE_SECURITY && !safe_mode_ready) return AIMT_UNAVAILABLE;
    policy->profile = profile;
    return AIMT_OK;
}

aimt_route_decision aimt_policy_route(const aimt_policy *policy, const aimt_route_request *request)
{
    if (policy == NULL || request == NULL || policy->profile < AIMT_PROFILE_PERFORMANCE ||
        policy->profile > AIMT_PROFILE_SECURITY || request->stage < AIMT_STAGE_PLAN ||
        request->stage > AIMT_STAGE_REVIEW || request->local_state < AIMT_LOCAL_NEW ||
        request->local_state > AIMT_LOCAL_DONE || policy->local_attempt_limit == 0)
        return (aimt_route_decision){AIMT_ROUTE_STOP, "Invalid policy or task."};
    if (request->local_state == AIMT_LOCAL_DONE)
        return (aimt_route_decision){AIMT_ROUTE_STOP, "Stage already complete."};
    bool cloud = policy->profile != AIMT_PROFILE_SECURITY && policy->cloud_allowed &&
        policy->cloud_connected && request->cloud_data_allowed &&
        policy->cloud_calls_used < policy->cloud_call_limit;
    if (cloud && ((request->stage == AIMT_STAGE_PLAN && policy->cloud_coordinator) ||
                  (request->stage == AIMT_STAGE_REVIEW && policy->cloud_reviewer)))
        return (aimt_route_decision){AIMT_ROUTE_CLOUD, "Cloud coordinator or reviewer role selected."};
    if (request->local_state != AIMT_LOCAL_NO_MEMORY && request->local_attempts < policy->local_attempt_limit)
        return (aimt_route_decision){AIMT_ROUTE_LOCAL, "Local work within the attempt limit."};
    if (cloud && policy->cloud_fallback)
        return (aimt_route_decision){AIMT_ROUTE_CLOUD, "Cloud assistance after local failure."};
    return (aimt_route_decision){AIMT_ROUTE_STOP, "No progress or resources; cloud unavailable or disallowed."};
}
