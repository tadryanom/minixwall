/*
 * Changes:
 *   Jan 22, 2010:  Created  (Cristiano Giuffrida)
 */

#include "inc.h"

/*===========================================================================*
 *				   do_up				     *
 *===========================================================================*/
PUBLIC int do_up(m_ptr)
message *m_ptr;					/* request message pointer */
{
/* A request was made to start a new system service. */
  struct rproc *rp;
  struct rprocpub *rpub;
  int r;
  struct rs_start rs_start;

  /* Check if the call can be allowed. */
  if((r = check_call_permission(m_ptr->m_source, RS_UP, NULL)) != OK)
      return r;

  /* Allocate a new system service slot. */
  r = alloc_slot(&rp);
  if(r != OK) {
      printf("RS: do_up: unable to allocate a new slot: %d\n", r);
      return r;
  }
  rpub = rp->r_pub;

  /* Copy the request structure. */
  r = copy_rs_start(m_ptr->m_source, m_ptr->RS_CMD_ADDR, &rs_start);
  if (r != OK) {
      return r;
  }

  /* Initialize the slot as requested. */
  r = init_slot(rp, &rs_start, m_ptr->m_source);
  if(r != OK) {
      printf("RS: do_up: unable to init the new slot: %d\n", r);
      return r;
  }

  /* Check for duplicates */
  if(lookup_slot_by_label(rpub->label)) {
      printf("RS: service '%s' (%d) has duplicate label\n", rpub->label,
          rpub->endpoint);
      return EBUSY;
  }

  /* All information was gathered. Now try to start the system service. */
  r = start_service(rp);
  activate_service(rp, NULL);
  if(r != OK) {
      return r;
  }

  /* Late reply - send a reply when service completes initialization. */
  rp->r_flags |= RS_LATEREPLY;
  rp->r_caller = m_ptr->m_source;
  rp->r_caller_request = RS_UP;

  return EDONTREPLY;
}

/*===========================================================================*
 *				do_down					     *
 *===========================================================================*/
PUBLIC int do_down(message *m_ptr)
{
  register struct rproc *rp;
  register struct rprocpub *rpub;
  int s;
  char label[RS_MAX_LABEL_LEN];

  /* Copy label. */
  s = copy_label(m_ptr->m_source, m_ptr->RS_CMD_ADDR,
      m_ptr->RS_CMD_LEN, label, sizeof(label));
  if(s != OK) {
      return s;
  }

  /* Lookup slot by label. */
  rp = lookup_slot_by_label(label);
  if(!rp) {
      if(rs_verbose)
          printf("RS: do_down: service '%s' not found\n", label);
      return(ESRCH);
  }
  rpub = rp->r_pub;

  /* Check if the call can be allowed. */
  if((s = check_call_permission(m_ptr->m_source, RS_DOWN, rp)) != OK)
      return s;

  /* Stop service. */
  if (rp->r_flags & RS_TERMINATED) {
        /* A recovery script is requesting us to bring down the service.
         * The service is already gone, simply perform cleanup.
         */
        if(rs_verbose)
            printf("RS: recovery script performs service down...\n");
  	unpublish_service(rp);
        unpublish_process(rp);
  	cleanup_service(rp);
    	return(OK);
  }
  stop_service(rp,RS_EXITING);

  /* Late reply - send a reply when service dies. */
  rp->r_flags |= RS_LATEREPLY;
  rp->r_caller = m_ptr->m_source;
  rp->r_caller_request = RS_DOWN;

  return EDONTREPLY;
}

/*===========================================================================*
 *				do_restart				     *
 *===========================================================================*/
PUBLIC int do_restart(message *m_ptr)
{
  struct rproc *rp;
  int s, r;
  char label[RS_MAX_LABEL_LEN];
  char script[MAX_SCRIPT_LEN];

  /* Copy label. */
  s = copy_label(m_ptr->m_source, m_ptr->RS_CMD_ADDR,
      m_ptr->RS_CMD_LEN, label, sizeof(label));
  if(s != OK) {
      return s;
  }

  /* Lookup slot by label. */
  rp = lookup_slot_by_label(label);
  if(!rp) {
      if(rs_verbose)
          printf("RS: do_restart: service '%s' not found\n", label);
      return(ESRCH);
  }

  /* Check if the call can be allowed. */
  if((r = check_call_permission(m_ptr->m_source, RS_RESTART, rp)) != OK)
      return r;

  /* We can only be asked to restart a service from a recovery script. */
  if (! (rp->r_flags & RS_TERMINATED) ) {
      if(rs_verbose)
          printf("RS: %s is still running\n", srv_to_string(rp));
      return EBUSY;
  }

  if(rs_verbose)
      printf("RS: recovery script performs service restart...\n");

  /* Restart the service, but make sure we don't call the script again. */
  strcpy(script, rp->r_script);
  rp->r_script[0] = '\0';
  restart_service(rp);
  strcpy(rp->r_script, script);

  return OK;
}


/*===========================================================================*
 *				do_refresh				     *
 *===========================================================================*/
PUBLIC int do_refresh(message *m_ptr)
{
  register struct rproc *rp;
  register struct rprocpub *rpub;
  int s;
  char label[RS_MAX_LABEL_LEN];

  /* Copy label. */
  s = copy_label(m_ptr->m_source, m_ptr->RS_CMD_ADDR,
      m_ptr->RS_CMD_LEN, label, sizeof(label));
  if(s != OK) {
      return s;
  }

  /* Lookup slot by label. */
  rp = lookup_slot_by_label(label);
  if(!rp) {
      if(rs_verbose)
          printf("RS: do_refresh: service '%s' not found\n", label);
      return(ESRCH);
  }
  rpub = rp->r_pub;

  /* Check if the call can be allowed. */
  if((s = check_call_permission(m_ptr->m_source, RS_REFRESH, rp)) != OK)
      return s;

  /* Refresh service. */
  if(rs_verbose)
      printf("RS: %s refreshing\n", srv_to_string(rp));
  stop_service(rp,RS_REFRESHING);

  return OK;
}

/*===========================================================================*
 *				do_shutdown				     *
 *===========================================================================*/
PUBLIC int do_shutdown(message *m_ptr)
{
  int slot_nr;
  struct rproc *rp;
  int r;

  /* Check if the call can be allowed. */
  if (m_ptr != NULL) {
      if((r = check_call_permission(m_ptr->m_source, RS_SHUTDOWN, NULL)) != OK)
          return r;
  }

  if(rs_verbose)
      printf("RS: shutting down...\n");

  /* Set flag to tell RS we are shutting down. */
  shutting_down = TRUE;

  /* Don't restart dead services. */
  for (slot_nr = 0; slot_nr < NR_SYS_PROCS; slot_nr++) {
      rp = &rproc[slot_nr];
      if (rp->r_flags & RS_IN_USE) {
          rp->r_flags |= RS_EXITING;
      }
  }
  return(OK);
}

/*===========================================================================*
 *				do_init_ready				     *
 *===========================================================================*/
PUBLIC int do_init_ready(message *m_ptr)
{
  int who_p;
  struct rproc *rp;
  struct rprocpub *rpub;
  int result;

  who_p = _ENDPOINT_P(m_ptr->m_source);
  rp = rproc_ptr[who_p];
  rpub = rp->r_pub;
  result = m_ptr->RS_INIT_RESULT;

  /* Make sure the originating service was requested to initialize. */
  if(! (rp->r_flags & RS_INITIALIZING) ) {
      if(rs_verbose)
          printf("RS: do_init_ready: got unexpected init ready msg from %d\n",
              m_ptr->m_source);
      return(EDONTREPLY);
  }

  /* Check if something went wrong and the service failed to init.
   * In that case, kill the service.
   */
  if(result != OK) {
      if(rs_verbose)
          printf("RS: %s initialization error: %s\n", srv_to_string(rp),
              init_strerror(result));
      crash_service(rp); /* simulate crash */
      return(EDONTREPLY);
  }

  /* Mark the slot as no longer initializing. */
  rp->r_flags &= ~RS_INITIALIZING;
  rp->r_check_tm = 0;
  getuptime(&rp->r_alive_tm);

  /* See if a late reply has to be sent. */
  late_reply(rp, OK);

  if(rs_verbose)
      printf("RS: %s initialized\n", srv_to_string(rp));

  /* If the service has completed initialization after a live
   * update, end the update now.
   */
  if(rp->r_flags & RS_UPDATING) {
      printf("RS: update succeeded\n");
      end_update(OK);
  }

  /* If the service has completed initialization after a crash
   * make the new instance active and cleanup the old replica.
   */
  if(rp->r_prev_rp) {
      activate_service(rp, rp->r_prev_rp);
      cleanup_service(rp->r_prev_rp);
      rp->r_prev_rp = NULL;

      if(rs_verbose)
          printf("RS: %s completed restart\n", srv_to_string(rp));
  }

  return(OK);
}

/*===========================================================================*
 *				do_update				     *
 *===========================================================================*/
PUBLIC int do_update(message *m_ptr)
{
  struct rproc *rp;
  struct rproc *new_rp;
  struct rprocpub *rpub;
  struct rs_start rs_start;
  int s;
  char label[RS_MAX_LABEL_LEN];
  int lu_state;
  int prepare_maxtime;

  /* Copy the request structure. */
  s = copy_rs_start(m_ptr->m_source, m_ptr->RS_CMD_ADDR, &rs_start);
  if (s != OK) {
      return s;
  }

  /* Copy label. */
  s = copy_label(m_ptr->m_source, rs_start.rss_label.l_addr,
      rs_start.rss_label.l_len, label, sizeof(label));
  if(s != OK) {
      return s;
  }

  /* Lookup slot by label. */
  rp = lookup_slot_by_label(label);
  if(!rp) {
      if(rs_verbose)
          printf("RS: do_update: service '%s' not found\n", label);
      return ESRCH;
  }
  rpub = rp->r_pub;

  /* Check if the call can be allowed. */
  if((s = check_call_permission(m_ptr->m_source, RS_UPDATE, rp)) != OK)
      return s;

  /* Retrieve live update state. */
  lu_state = m_ptr->RS_LU_STATE;
  if(lu_state == SEF_LU_STATE_NULL) {
      return(EINVAL);
  }

  /* Retrieve prepare max time. */
  prepare_maxtime = m_ptr->RS_LU_PREPARE_MAXTIME;
  if(prepare_maxtime) {
      if(prepare_maxtime < 0 || prepare_maxtime > RS_MAX_PREPARE_MAXTIME) {
          return(EINVAL);
      }
  }
  else {
      prepare_maxtime = RS_DEFAULT_PREPARE_MAXTIME;
  }

  /* Make sure we are not already updating. */
  if(rupdate.flags & RS_UPDATING) {
      if(rs_verbose)
	  printf("RS: do_update: an update is already in progress\n");
      return EBUSY;
  }

  /* Allocate a system service slot for the new version. */
  s = alloc_slot(&new_rp);
  if(s != OK) {
      printf("RS: do_update: unable to allocate a new slot: %d\n", s);
      return s;
  }

  /* Initialize the slot as requested. */
  s = init_slot(new_rp, &rs_start, m_ptr->m_source);
  if(s != OK) {
      printf("RS: do_update: unable to init the new slot: %d\n", s);
      return s;
  }

  /* Let the new version inherit defaults from the old one. */
  inherit_service_defaults(rp, new_rp);

  /* Create new version of the service but don't let it run. */
  s = create_service(new_rp);
  if(s != OK) {
      printf("RS: do_update: unable to create a new service: %d\n", s);
      return s;
  }

  /* Publish process-wide properties. */
  s = publish_process(new_rp);
  if (s != OK) {
      printf("RS: do_update: publish_process failed: %d\n", s);
      return s;
  }

  /* Link old version to new version and mark both as updating. */
  rp->r_new_rp = new_rp;
  new_rp->r_old_rp = rp;
  rp->r_flags |= RS_UPDATING;
  rp->r_new_rp->r_flags |= RS_UPDATING;
  rupdate.flags |= RS_UPDATING;
  getuptime(&rupdate.prepare_tm);
  rupdate.prepare_maxtime = prepare_maxtime;
  rupdate.rp = rp;

  if(rs_verbose)
    printf("RS: %s updating\n", srv_to_string(rp));

  /* Request to update. */
  m_ptr->m_type = RS_LU_PREPARE;
  asynsend3(rpub->endpoint, m_ptr, AMF_NOREPLY);

  /* Late reply - send a reply when the new version completes initialization. */
  rp->r_flags |= RS_LATEREPLY;
  rp->r_caller = m_ptr->m_source;
  rp->r_caller_request = RS_UPDATE;

  return EDONTREPLY;
}

/*===========================================================================*
 *				do_upd_ready				     *
 *===========================================================================*/
PUBLIC int do_upd_ready(message *m_ptr)
{
  struct rproc *rp, *old_rp, *new_rp;
  int who_p;
  int result;
  int r;

  who_p = _ENDPOINT_P(m_ptr->m_source);
  rp = rproc_ptr[who_p];
  result = m_ptr->RS_LU_RESULT;

  /* Make sure the originating service was requested to prepare for update. */
  if(rp != rupdate.rp) {
      if(rs_verbose)
          printf("RS: do_upd_ready: got unexpected update ready msg from %d\n",
              m_ptr->m_source);
      return(EINVAL);
  }

  /* Check if something went wrong and the service failed to prepare
   * for the update. In that case, end the update process. The old version will
   * be replied to and continue executing.
   */
  if(result != OK) {
      end_update(result);

      printf("RS: update failed: %s\n", lu_strerror(result));
      return OK;
  }

  /* Perform the update. */
  old_rp = rp;
  new_rp = rp->r_new_rp;
  r = update_service(&old_rp, &new_rp);
  if(r != OK) {
      end_update(r);
      printf("RS: update failed: error %d\n", r);
      return r;
  }

  /* Let the new version run. */
  r = run_service(new_rp, SEF_INIT_LU);
  if(r != OK) {
      update_service(&new_rp, &old_rp); /* rollback, can't fail. */
      end_update(r);
      printf("RS: update failed: error %d\n", r);
      return r;
  }

  return(EDONTREPLY);
}

/*===========================================================================*
 *				do_period				     *
 *===========================================================================*/
PUBLIC void do_period(m_ptr)
message *m_ptr;
{
  register struct rproc *rp;
  register struct rprocpub *rpub;
  clock_t now = m_ptr->NOTIFY_TIMESTAMP;
  int s;
  long period;

  /* If an update is in progress, check its status. */
  if(rupdate.flags & RS_UPDATING) {
      update_period(m_ptr);
  }

  /* Search system services table. Only check slots that are in use and not
   * updating.
   */
  for (rp=BEG_RPROC_ADDR; rp<END_RPROC_ADDR; rp++) {
      rpub = rp->r_pub;
      if ((rp->r_flags & RS_IN_USE) && !(rp->r_flags & RS_UPDATING)) {

          /* Compute period. */
          period = rpub->period;
          if(rp->r_flags & RS_INITIALIZING) {
              period = RS_INIT_T;
          }

          /* If the service is to be revived (because it repeatedly exited, 
	   * and was not directly restarted), the binary backoff field is  
	   * greater than zero. 
	   */
	  if (rp->r_backoff > 0) {
              rp->r_backoff -= 1;
	      if (rp->r_backoff == 0) {
		  restart_service(rp);
	      }
	  }

	  /* If the service was signaled with a SIGTERM and fails to respond,
	   * kill the system service with a SIGKILL signal.
	   */
	  else if (rp->r_stop_tm > 0 && now - rp->r_stop_tm > 2*RS_DELTA_T
	   && rp->r_pid > 0) {
              crash_service(rp); /* simulate crash */
              rp->r_stop_tm = 0;
	  }

	  /* There seems to be no special conditions. If the service has a 
	   * period assigned check its status. 
	   */
	  else if (period > 0) {

	      /* Check if an answer to a status request is still pending. If 
	       * the service didn't respond within time, kill it to simulate 
	       * a crash. The failure will be detected and the service will 
	       * be restarted automatically.
	       */
              if (rp->r_alive_tm < rp->r_check_tm) { 
	          if (now - rp->r_alive_tm > 2*period &&
		      rp->r_pid > 0 && !(rp->r_flags & RS_NOPINGREPLY)) { 
		      if(rs_verbose)
                           printf("RS: %s reported late\n",
				srv_to_string(rp)); 
		      rp->r_flags |= RS_NOPINGREPLY;
                      crash_service(rp); /* simulate crash */
		  }
	      }

	      /* No answer pending. Check if a period expired since the last
	       * check and, if so request the system service's status.
	       */
	      else if (now - rp->r_check_tm > rpub->period) {
		  notify(rpub->endpoint);		/* request status */
		  rp->r_check_tm = now;			/* mark time */
              }
          }
      }
  }

  /* Reschedule a synchronous alarm for the next period. */
  if (OK != (s=sys_setalarm(RS_DELTA_T, 0)))
      panic("couldn't set alarm: %d", s);
}

/*===========================================================================*
 *			          do_sigchld				     *
 *===========================================================================*/
PUBLIC void do_sigchld()
{
/* PM informed us that there are dead children to cleanup. Go get them. */
  pid_t pid;
  int status;
  struct rproc *rp;
  struct rproc **rps;
  struct rprocpub *rpub;
  int i, nr_rps;

  if(rs_verbose)
     printf("RS: got SIGCHLD signal, cleaning up dead children\n");

  while ( (pid = waitpid(-1, &status, WNOHANG)) != 0 ) {
      rp = lookup_slot_by_pid(pid);
      if(rp != NULL) {
          rpub = rp->r_pub;

          if(rs_verbose)
              printf("RS: %s exited via another signal manager\n",
                  srv_to_string(rp));

          /* The slot is still there. This means RS is not the signal
           * manager assigned to the process. Ignore the event but
           * free slots for all the service instances and send a late
           * reply if necessary.
           */
             get_service_instances(rp, &rps, &nr_rps);
             for(i=0;i<nr_rps;i++) {
                 if(rupdate.flags & RS_UPDATING) {
                     rupdate.flags &= ~RS_UPDATING;
                 }
                 free_slot(rps[i]);
             }
      }
  }
}

/*===========================================================================*
 *				do_getsysinfo				     *
 *===========================================================================*/
PUBLIC int do_getsysinfo(m_ptr)
message *m_ptr;
{
  vir_bytes src_addr, dst_addr;
  int dst_proc;
  size_t len;
  int s;

  /* Check if the call can be allowed. */
  if((s = check_call_permission(m_ptr->m_source, 0, NULL)) != OK)
      return s;

  switch(m_ptr->m1_i1) {
  case SI_PROC_TAB:
  	src_addr = (vir_bytes) rproc;
  	len = sizeof(struct rproc) * NR_SYS_PROCS;
  	break; 
  case SI_PROCPUB_TAB:
  	src_addr = (vir_bytes) rprocpub;
  	len = sizeof(struct rprocpub) * NR_SYS_PROCS;
  	break; 
  default:
  	return(EINVAL);
  }

  dst_proc = m_ptr->m_source;
  dst_addr = (vir_bytes) m_ptr->m1_p1;
  if (OK != (s=sys_datacopy(SELF, src_addr, dst_proc, dst_addr, len)))
  	return(s);
  return(OK);
}

/*===========================================================================*
 *				do_lookup				     *
 *===========================================================================*/
PUBLIC int do_lookup(m_ptr)
message *m_ptr;
{
	static char namebuf[100];
	int len, r;
	struct rproc *rrp;
	struct rprocpub *rrpub;

	len = m_ptr->RS_NAME_LEN;

	if(len < 2 || len >= sizeof(namebuf)) {
		printf("RS: len too weird (%d)\n", len);
		return EINVAL;
	}

	if((r=sys_vircopy(m_ptr->m_source, D, (vir_bytes) m_ptr->RS_NAME,
		SELF, D, (vir_bytes) namebuf, len)) != OK) {
		printf("RS: name copy failed\n");
		return r;

	}

	namebuf[len] = '\0';

	rrp = lookup_slot_by_label(namebuf);
	if(!rrp) {
		return ESRCH;
	}
	rrpub = rrp->r_pub;
	m_ptr->RS_ENDPOINT = rrpub->endpoint;

	return OK;
}
