#include "sehandlers.h"
#include "config.h"
#include "comments.h"
#include "common.h"
#include "statusdata.h"
#include "downtime.h"
#include "macros.h"
#include "perfdata.h"
#include "broker.h"
#include "workers.h"
#include "utils.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"
#include <string.h>

#ifdef USE_EVENT_BROKER
#include "neberrors.h"
#endif

struct obsessive_compulsive_job {
	host *hst;
	service *svc;
};

/******************************************************************/
/************* OBSESSIVE COMPULSIVE HANDLER FUNCTIONS *************/
/******************************************************************/

void obsessive_compulsive_job_handler(struct wproc_result *wpres, void *data, int flags) {
	struct obsessive_compulsive_job *ocj = (struct obsessive_compulsive_job *)data;
	if (wpres) {
		if (wpres->early_timeout) {
			if(ocj->svc) {
				logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Timeout while executing OCSP command '%s' for service '%s' on host '%s'\n",
						wpres->command, ocj->svc->description, ocj->hst->name);
			} else {
				logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Timeout while executing OCHP command '%s' for host '%s'\n",
						wpres->command, ocj->hst->name);
			}
		}
	}
	free(ocj);
}


/* handles service check results in an obsessive compulsive manner... */
int obsessive_compulsive_service_check_processor(service *svc)
{
	char *raw_command = NULL;
	char *processed_command = NULL;
	host *temp_host = NULL;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
	nagios_macros mac;
	struct obsessive_compulsive_job *ocj;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "obsessive_compulsive_service_check_processor()\n");

	if (svc == NULL)
		return ERROR;

	/* bail out if we shouldn't be obsessing */
	if (obsess_over_services == FALSE || svc->obsess == FALSE)
		return OK;

	/* if there is no valid command, exit */
	if (ocsp_command == NULL)
		return ERROR;

	/* find the associated host */
	if ((temp_host = (host *)svc->host_ptr) == NULL)
		return ERROR;

	/* update service macros */
	memset(&mac, 0, sizeof(mac));
	grab_host_macros_r(&mac, temp_host);
	grab_service_macros_r(&mac, svc);

	/* get the raw command line */
	get_raw_command_line_r(&mac, ocsp_command_ptr, ocsp_command, &raw_command, macro_options);
	if (raw_command == NULL) {
		clear_volatile_macros_r(&mac);
		return ERROR;
	}

	log_debug_info(DEBUGL_CHECKS, 2, "Raw obsessive compulsive service processor command line: %s\n", raw_command);

	/* process any macros in the raw command line */
	process_macros_r(&mac, raw_command, &processed_command, macro_options);
	my_free(raw_command);
	if (processed_command == NULL) {
		clear_volatile_macros_r(&mac);
		return ERROR;
	}

	log_debug_info(DEBUGL_CHECKS, 2, "Processed obsessive compulsive service processor command line: %s\n", processed_command);

	/* run the command through a worker */
	ocj = nm_calloc(1,sizeof(struct obsessive_compulsive_job));
	ocj->hst = svc->host_ptr;
	ocj->svc = svc;
	if(ERROR == wproc_run_callback(processed_command, ocsp_timeout, obsessive_compulsive_job_handler, ocj, &mac)) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Unable to start OCSP job for service '%s on host '%s' to worker\n", svc->description, svc->host_ptr->name);
		free(ocj);
	}

	/* free memory */
	clear_volatile_macros_r(&mac);
	my_free(processed_command);

	return OK;
}


/* handles host check results in an obsessive compulsive manner... */
int obsessive_compulsive_host_check_processor(host *hst)
{
	char *raw_command = NULL;
	char *processed_command = NULL;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
	nagios_macros mac;
	struct obsessive_compulsive_job *ocj;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "obsessive_compulsive_host_check_processor()\n");

	if (hst == NULL)
		return ERROR;

	/* bail out if we shouldn't be obsessing */
	if (obsess_over_hosts == FALSE || hst->obsess == FALSE)
		return OK;

	/* if there is no valid command, exit */
	if (ochp_command == NULL)
		return ERROR;

	/* update macros */
	memset(&mac, 0, sizeof(mac));
	grab_host_macros_r(&mac, hst);

	/* get the raw command line */
	get_raw_command_line_r(&mac, ochp_command_ptr, ochp_command, &raw_command, macro_options);
	if (raw_command == NULL) {
		clear_volatile_macros_r(&mac);
		return ERROR;
	}

	log_debug_info(DEBUGL_CHECKS, 2, "Raw obsessive compulsive host processor command line: %s\n", raw_command);

	/* process any macros in the raw command line */
	process_macros_r(&mac, raw_command, &processed_command, macro_options);
	my_free(raw_command);
	if (processed_command == NULL) {
		clear_volatile_macros_r(&mac);
		return ERROR;
	}

	log_debug_info(DEBUGL_CHECKS, 2, "Processed obsessive compulsive host processor command line: %s\n", processed_command);

	/* run the command through a worker */
	ocj = nm_calloc(1,sizeof(struct obsessive_compulsive_job));
	ocj->hst = hst;
	ocj->svc = NULL;
	if(ERROR == wproc_run_callback(processed_command, ochp_timeout, obsessive_compulsive_job_handler, ocj, &mac)) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Unable to start OCHP job for host '%s' to worker\n", hst->name);
		free(ocj);
	}

	/* free memory */
	clear_volatile_macros_r(&mac);
	my_free(processed_command);

	return OK;
}


/******************************************************************/
/**************** SERVICE EVENT HANDLER FUNCTIONS *****************/
/******************************************************************/

void event_handler_job_handler(struct wproc_result *wpres, void *data, int flags) {
	const char *event_type = (const char*)data;
	if(wpres) {
		if (wpres->early_timeout) {
			logit(NSLOG_EVENT_HANDLER | NSLOG_RUNTIME_WARNING, TRUE,
			      "Warning: %s handler command '%s' timed out\n",
			      event_type, wpres->command);
		}
	}
}

/* handles changes in the state of a service */
int handle_service_event(service *svc)
{
	host *temp_host = NULL;
	nagios_macros mac;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "handle_service_event()\n");

	if (svc == NULL)
		return ERROR;

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	broker_statechange_data(NEBTYPE_STATECHANGE_END, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_STATECHANGE, (void *)svc, svc->current_state, svc->state_type, svc->current_attempt, svc->max_attempts, NULL);
#endif

	/* bail out if we shouldn't be running event handlers */
	if (enable_event_handlers == FALSE)
		return OK;
	if (svc->event_handler_enabled == FALSE)
		return OK;

	/* find the host */
	if ((temp_host = (host *)svc->host_ptr) == NULL)
		return ERROR;

	/* update service macros */
	memset(&mac, 0, sizeof(mac));
	grab_host_macros_r(&mac, temp_host);
	grab_service_macros_r(&mac, svc);

	/* run the global service event handler */
	run_global_service_event_handler(&mac, svc);

	/* run the event handler command if there is one */
	if (svc->event_handler != NULL)
		run_service_event_handler(&mac, svc);
	clear_volatile_macros_r(&mac);

	return OK;
}


/* runs the global service event handler */
int run_global_service_event_handler(nagios_macros *mac, service *svc)
{
	char *raw_command = NULL;
	char *processed_command = NULL;
	char *raw_logentry = NULL;
	char *processed_logentry = NULL;
	char *command_output = NULL;
	int early_timeout = FALSE;
	double exectime = 0.0;
	int result = 0;
#ifdef USE_EVENT_BROKER
	struct timeval start_time;
	struct timeval end_time;
	int neb_result = OK;
#endif
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "run_global_service_event_handler()\n");

	if (svc == NULL)
		return ERROR;

	/* bail out if we shouldn't be running event handlers */
	if (enable_event_handlers == FALSE)
		return OK;

	/* a global service event handler command has not been defined */
	if (global_service_event_handler == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 1, "Running global event handler for service '%s' on host '%s'...\n", svc->description, svc->host_name);

#ifdef USE_EVENT_BROKER
	/* get start time */
	gettimeofday(&start_time, NULL);
#endif

	/* get the raw command line */
	get_raw_command_line_r(mac, global_service_event_handler_ptr, global_service_event_handler, &raw_command, macro_options);
	if (raw_command == NULL) {
		return ERROR;
	}

	log_debug_info(DEBUGL_EVENTHANDLERS, 2, "Raw global service event handler command line: %s\n", raw_command);

	/* process any macros in the raw command line */
	process_macros_r(mac, raw_command, &processed_command, macro_options);
	my_free(raw_command);
	if (processed_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 2, "Processed global service event handler command line: %s\n", processed_command);

	if (log_event_handlers == TRUE) {
		nm_asprintf(&raw_logentry, "GLOBAL SERVICE EVENT HANDLER: %s;%s;$SERVICESTATE$;$SERVICESTATETYPE$;$SERVICEATTEMPT$;%s\n", svc->host_name, svc->description, global_service_event_handler);
		process_macros_r(mac, raw_logentry, &processed_logentry, macro_options);
		logit(NSLOG_EVENT_HANDLER, FALSE, "%s", processed_logentry);
	}

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	neb_result = broker_event_handler(NEBTYPE_EVENTHANDLER_START, NEBFLAG_NONE, NEBATTR_NONE, GLOBAL_SERVICE_EVENTHANDLER, (void *)svc, svc->current_state, svc->state_type, start_time, end_time, exectime, event_handler_timeout, early_timeout, result, global_service_event_handler, processed_command, NULL, NULL);

	/* neb module wants to override (or cancel) the event handler - perhaps it will run the eventhandler itself */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE) {
		my_free(processed_command);
		my_free(raw_logentry);
		my_free(processed_logentry);
		return OK;
	}
#endif

	/* run the command through a worker */
	result = wproc_run_callback(processed_command, event_handler_timeout, event_handler_job_handler, "Global service", mac);

	/* check to see if the event handler timed out */
	if (early_timeout == TRUE)
		logit(NSLOG_EVENT_HANDLER | NSLOG_RUNTIME_WARNING, TRUE, "Warning: Global service event handler command '%s' timed out after %d seconds\n", processed_command, event_handler_timeout);

#ifdef USE_EVENT_BROKER
	/* get end time */
	gettimeofday(&end_time, NULL);
#endif

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	broker_event_handler(NEBTYPE_EVENTHANDLER_END, NEBFLAG_NONE, NEBATTR_NONE, GLOBAL_SERVICE_EVENTHANDLER, (void *)svc, svc->current_state, svc->state_type, start_time, end_time, exectime, event_handler_timeout, early_timeout, result, global_service_event_handler, processed_command, command_output, NULL);
#endif

	/* free memory */
	my_free(command_output);
	my_free(processed_command);
	my_free(raw_logentry);
	my_free(processed_logentry);

	return OK;
}


/* runs a service event handler command */
int run_service_event_handler(nagios_macros *mac, service *svc)
{
	char *raw_command = NULL;
	char *processed_command = NULL;
	char *raw_logentry = NULL;
	char *processed_logentry = NULL;
	char *command_output = NULL;
	int early_timeout = FALSE;
	double exectime = 0.0;
	int result = 0;
#ifdef USE_EVENT_BROKER
	struct timeval start_time;
	struct timeval end_time;
	int neb_result = OK;
#endif
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "run_service_event_handler()\n");

	if (svc == NULL)
		return ERROR;

	/* bail if there's no command */
	if (svc->event_handler == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 1, "Running event handler for service '%s' on host '%s'...\n", svc->description, svc->host_name);

#ifdef USE_EVENT_BROKER
	/* get start time */
	gettimeofday(&start_time, NULL);
#endif


	/* get the raw command line */
	get_raw_command_line_r(mac, svc->event_handler_ptr, svc->event_handler, &raw_command, macro_options);
	if (raw_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 2, "Raw service event handler command line: %s\n", raw_command);

	/* process any macros in the raw command line */
	process_macros_r(mac, raw_command, &processed_command, macro_options);
	my_free(raw_command);
	if (processed_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 2, "Processed service event handler command line: %s\n", processed_command);

	if (log_event_handlers == TRUE) {
		nm_asprintf(&raw_logentry, "SERVICE EVENT HANDLER: %s;%s;$SERVICESTATE$;$SERVICESTATETYPE$;$SERVICEATTEMPT$;%s\n", svc->host_name, svc->description, svc->event_handler);
		process_macros_r(mac, raw_logentry, &processed_logentry, macro_options);
		logit(NSLOG_EVENT_HANDLER, FALSE, "%s", processed_logentry);
	}

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	neb_result = broker_event_handler(NEBTYPE_EVENTHANDLER_START, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_EVENTHANDLER, (void *)svc, svc->current_state, svc->state_type, start_time, end_time, exectime, event_handler_timeout, early_timeout, result, svc->event_handler, processed_command, NULL, NULL);

	/* neb module wants to override (or cancel) the event handler - perhaps it will run the eventhandler itself */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE) {
		my_free(processed_command);
		my_free(raw_logentry);
		my_free(processed_logentry);
		return OK;
	}
#endif

	/* run the command through a worker */
	result = wproc_run_callback(processed_command, event_handler_timeout, event_handler_job_handler, "Service", mac);

	/* check to see if the event handler timed out */
	if (early_timeout == TRUE)
		logit(NSLOG_EVENT_HANDLER | NSLOG_RUNTIME_WARNING, TRUE, "Warning: Service event handler command '%s' timed out after %d seconds\n", processed_command, event_handler_timeout);

#ifdef USE_EVENT_BROKER
	/* get end time */
	gettimeofday(&end_time, NULL);
#endif

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	broker_event_handler(NEBTYPE_EVENTHANDLER_END, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_EVENTHANDLER, (void *)svc, svc->current_state, svc->state_type, start_time, end_time, exectime, event_handler_timeout, early_timeout, result, svc->event_handler, processed_command, command_output, NULL);
#endif

	/* free memory */
	my_free(command_output);
	my_free(processed_command);
	my_free(raw_logentry);
	my_free(processed_logentry);

	return OK;
}


/******************************************************************/
/****************** HOST EVENT HANDLER FUNCTIONS ******************/
/******************************************************************/

/* handles a change in the status of a host */
int handle_host_event(host *hst)
{
	nagios_macros mac;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "handle_host_event()\n");

	if (hst == NULL)
		return ERROR;

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	broker_statechange_data(NEBTYPE_STATECHANGE_END, NEBFLAG_NONE, NEBATTR_NONE, HOST_STATECHANGE, (void *)hst, hst->current_state, hst->state_type, hst->current_attempt, hst->max_attempts, NULL);
#endif

	/* bail out if we shouldn't be running event handlers */
	if (enable_event_handlers == FALSE)
		return OK;
	if (hst->event_handler_enabled == FALSE)
		return OK;

	/* update host macros */
	memset(&mac, 0, sizeof(mac));
	grab_host_macros_r(&mac, hst);

	/* run the global host event handler */
	run_global_host_event_handler(&mac, hst);

	/* run the event handler command if there is one */
	if (hst->event_handler != NULL)
		run_host_event_handler(&mac, hst);

	return OK;
}


/* runs the global host event handler */
int run_global_host_event_handler(nagios_macros *mac, host *hst)
{
	char *raw_command = NULL;
	char *processed_command = NULL;
	char *raw_logentry = NULL;
	char *processed_logentry = NULL;
	char *command_output = NULL;
	int early_timeout = FALSE;
	double exectime = 0.0;
	int result = 0;
#ifdef USE_EVENT_BROKER
	struct timeval start_time;
	struct timeval end_time;
	int neb_result = OK;
#endif
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "run_global_host_event_handler()\n");

	if (hst == NULL)
		return ERROR;

	/* bail out if we shouldn't be running event handlers */
	if (enable_event_handlers == FALSE)
		return OK;

	/* no global host event handler command is defined */
	if (global_host_event_handler == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 1, "Running global event handler for host '%s'..\n", hst->name);

#ifdef USE_EVENT_BROKER
	/* get start time */
	gettimeofday(&start_time, NULL);
#endif

	/* get the raw command line */
	get_raw_command_line_r(mac, global_host_event_handler_ptr, global_host_event_handler, &raw_command, macro_options);
	if (raw_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 2, "Raw global host event handler command line: %s\n", raw_command);

	/* process any macros in the raw command line */
	process_macros_r(mac, raw_command, &processed_command, macro_options);
	my_free(raw_command);
	if (processed_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 2, "Processed global host event handler command line: %s\n", processed_command);

	if (log_event_handlers == TRUE) {
		nm_asprintf(&raw_logentry, "GLOBAL HOST EVENT HANDLER: %s;$HOSTSTATE$;$HOSTSTATETYPE$;$HOSTATTEMPT$;%s\n", hst->name, global_host_event_handler);
		process_macros_r(mac, raw_logentry, &processed_logentry, macro_options);
		logit(NSLOG_EVENT_HANDLER, FALSE, "%s", processed_logentry);
	}

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	neb_result = broker_event_handler(NEBTYPE_EVENTHANDLER_START, NEBFLAG_NONE, NEBATTR_NONE, GLOBAL_HOST_EVENTHANDLER, (void *)hst, hst->current_state, hst->state_type, start_time, end_time, exectime, event_handler_timeout, early_timeout, result, global_host_event_handler, processed_command, NULL, NULL);

	/* neb module wants to override (or cancel) the event handler - perhaps it will run the eventhandler itself */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE) {
		my_free(processed_command);
		my_free(raw_logentry);
		my_free(processed_logentry);
		return OK;
	}
#endif

	/* run the command through a worker */
	result = wproc_run_callback(processed_command, event_handler_timeout, event_handler_job_handler, "Global host", mac);

	/* check for a timeout in the execution of the event handler command */
	if (early_timeout == TRUE)
		logit(NSLOG_EVENT_HANDLER | NSLOG_RUNTIME_WARNING, TRUE, "Warning: Global host event handler command '%s' timed out after %d seconds\n", processed_command, event_handler_timeout);

#ifdef USE_EVENT_BROKER
	/* get end time */
	gettimeofday(&end_time, NULL);
#endif

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	broker_event_handler(NEBTYPE_EVENTHANDLER_END, NEBFLAG_NONE, NEBATTR_NONE, GLOBAL_HOST_EVENTHANDLER, (void *)hst, hst->current_state, hst->state_type, start_time, end_time, exectime, event_handler_timeout, early_timeout, result, global_host_event_handler, processed_command, command_output, NULL);
#endif

	/* free memory */
	my_free(command_output);
	my_free(processed_command);
	my_free(raw_logentry);
	my_free(processed_logentry);

	return OK;
}


/* runs a host event handler command */
int run_host_event_handler(nagios_macros *mac, host *hst)
{
	char *raw_command = NULL;
	char *processed_command = NULL;
	char *raw_logentry = NULL;
	char *processed_logentry = NULL;
	char *command_output = NULL;
	int early_timeout = FALSE;
	double exectime = 0.0;
	int result = 0;
#ifdef USE_EVENT_BROKER
	struct timeval start_time;
	struct timeval end_time;
	int neb_result = OK;
#endif
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "run_host_event_handler()\n");

	if (hst == NULL)
		return ERROR;

	/* bail if there's no command */
	if (hst->event_handler == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 1, "Running event handler for host '%s'..\n", hst->name);

#ifdef USE_EVENT_BROKER
	/* get start time */
	gettimeofday(&start_time, NULL);
#endif

	/* get the raw command line */
	get_raw_command_line_r(mac, hst->event_handler_ptr, hst->event_handler, &raw_command, macro_options);
	if (raw_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 2, "Raw host event handler command line: %s\n", raw_command);

	/* process any macros in the raw command line */
	process_macros_r(mac, raw_command, &processed_command, macro_options);
	my_free(raw_command);
	if (processed_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTHANDLERS, 2, "Processed host event handler command line: %s\n", processed_command);

	if (log_event_handlers == TRUE) {
		nm_asprintf(&raw_logentry, "HOST EVENT HANDLER: %s;$HOSTSTATE$;$HOSTSTATETYPE$;$HOSTATTEMPT$;%s\n", hst->name, hst->event_handler);
		process_macros_r(mac, raw_logentry, &processed_logentry, macro_options);
		logit(NSLOG_EVENT_HANDLER, FALSE, "%s", processed_logentry);
	}

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	neb_result = broker_event_handler(NEBTYPE_EVENTHANDLER_START, NEBFLAG_NONE, NEBATTR_NONE, HOST_EVENTHANDLER, (void *)hst, hst->current_state, hst->state_type, start_time, end_time, exectime, event_handler_timeout, early_timeout, result, hst->event_handler, processed_command, NULL, NULL);

	/* neb module wants to override (or cancel) the event handler - perhaps it will run the eventhandler itself */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE) {
		my_free(processed_command);
		my_free(raw_logentry);
		my_free(processed_logentry);
		return OK;
	}
#endif

	/* run the command through a worker */
	result = wproc_run_callback(processed_command, event_handler_timeout, event_handler_job_handler, "Host", mac);

	/* check to see if the event handler timed out */
	if (early_timeout == TRUE)
		logit(NSLOG_EVENT_HANDLER | NSLOG_RUNTIME_WARNING, TRUE, "Warning: Host event handler command '%s' timed out after %d seconds\n", processed_command, event_handler_timeout);

#ifdef USE_EVENT_BROKER
	/* get end time */
	gettimeofday(&end_time, NULL);
#endif

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	broker_event_handler(NEBTYPE_EVENTHANDLER_END, NEBFLAG_NONE, NEBATTR_NONE, HOST_EVENTHANDLER, (void *)hst, hst->current_state, hst->state_type, start_time, end_time, exectime, event_handler_timeout, early_timeout, result, hst->event_handler, processed_command, command_output, NULL);
#endif

	/* free memory */
	my_free(command_output);
	my_free(processed_command);
	my_free(raw_logentry);
	my_free(processed_logentry);

	return OK;
}
