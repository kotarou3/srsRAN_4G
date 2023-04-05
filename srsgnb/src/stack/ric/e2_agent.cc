/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsgnb/hdr/stack/ric/e2_agent.h"
#include "srsran/asn1/e2ap.h"

using namespace srsenb;
e2_agent::e2_agent(srslog::basic_logger& logger, e2_interface_metrics* _gnb_metrics) :
  task_sched(), logger(logger), rx_sockets(), thread("E2_AGENT_THREAD"), e2ap_(logger, this, _gnb_metrics, &task_sched)
{
  gnb_metrics = _gnb_metrics;
}

bool e2_agent::init(e2_agent_args_t args)
{
  printf("E2_AGENT: Init\n");
  using namespace srsran::net_utils;
  // Open SCTP socket
  if (not ric_socket.open_socket(addr_family::ipv4, socket_type::seqpacket, protocol_type::SCTP)) {
    return false;
  }
  printf("RIC SCTP socket opened. fd=%d\n", ric_socket.fd());
  if (not ric_socket.sctp_subscribe_to_events()) {
    ric_socket.close();
    return false;
  }

  // Bind socket
  if (not ric_socket.bind_addr(args.ric_bind_ip.c_str(), args.ric_bind_port)) {
    ric_socket.close();
    return false;
  }

  // Connect to the AMF address
  if (not ric_socket.connect_to(args.ric_ip.c_str(), args.ric_port, &ric_addr)) {
    return false;
  }
  // Assign a handler to rx RIC packets
  ric_rece_task_queue = task_sched.make_task_queue();
  auto rx_callback =
      [this](srsran::unique_byte_buffer_t pdu, const sockaddr_in& from, const sctp_sndrcvinfo& sri, int flags) {
        handle_e2_rx_msg(std::move(pdu), from, sri, flags);
      };
  rx_sockets.add_socket_handler(ric_socket.fd(),
                                srsran::make_sctp_sdu_handler(logger, ric_rece_task_queue, rx_callback));

  printf("SCTP socket connected with RIC. fd=%d \n", ric_socket.fd());
  running = true;
  start(0);
  return SRSRAN_SUCCESS;
}

void e2_agent::stop()
{
  running = false;
  wait_thread_finish();
}

void e2_agent::tic()
{
  // get tick every 1ms to advance timers
  task_sched.tic();
}

void e2_agent::run_thread()
{
  using namespace asn1::e2ap;

  while (running) {
    if (e2ap_.send_setup_request()) {
      send_e2_msg(E2_SETUP_REQUEST);
      printf("e2 setup request sent\n");
    }
    task_sched.run_next_task();
  }
}

bool e2_agent::send_sctp(srsran::unique_byte_buffer_t& buf)
{
  ssize_t ret;
  ret = sctp_sendmsg(ric_socket.fd(),
                     buf->msg,
                     buf->N_bytes,
                     (struct sockaddr*)&ric_addr,
                     sizeof(ric_addr),
                     htonl(e2ap_ppid),
                     0,
                     0,
                     0,
                     0);
  if (ret == -1) {
    printf("failed to send %d bytes\n", buf->N_bytes);
    return false;
  }
  return true;
}

bool e2_agent::send_e2_msg(e2_msg_type_t msg_type)
{
  std::string message_name;
  e2_ap_pdu_c send_pdu;

  switch (msg_type) {
    case e2_msg_type_t::E2_SETUP_REQUEST:
      send_pdu     = e2ap_.generate_setup_request();
      message_name = "E2 SETUP REQUEST";
      break;
    case e2_msg_type_t::E2_RESET:
      send_pdu     = e2ap_.generate_reset_request();
      message_name = "E2 RESET REQUEST";
      break;
    case e2_msg_type_t::E2_RESET_RESPONSE:
      send_pdu     = e2ap_.generate_reset_response();
      message_name = "E2 RESET RESPONSE";
      break;
    default:
      printf("Unknown E2AP message type\n");
      return false;
  }

  return send_e2ap_pdu(send_pdu);
}

bool e2_agent::queue_send_e2ap_pdu(e2_ap_pdu_c e2ap_pdu)
{
  auto send_e2ap_pdu_task = [this, e2ap_pdu]() { send_e2ap_pdu(e2ap_pdu); };
  ric_rece_task_queue.push(send_e2ap_pdu_task);
  return true;
}

bool e2_agent::send_e2ap_pdu(e2_ap_pdu_c send_pdu)
{
  srsran::unique_byte_buffer_t buf = srsran::make_byte_buffer();
  if (buf == nullptr) {
    // logger.error("Fatal Error: Couldn't allocate buffer for %s.", procedure_name);
    return false;
  }
  asn1::bit_ref bref(buf->msg, buf->get_tailroom());
  if (send_pdu.pack(bref) != asn1::SRSASN_SUCCESS) {
    logger.error("Failed to pack TX E2 PDU");
    return false;
  }
  buf->N_bytes = bref.distance_bytes();
  printf("try to send %d bytes to addr %s \n", buf->N_bytes, inet_ntoa(ric_addr.sin_addr));
  if (!send_sctp(buf)) {
    logger.error("failed to send");
    return false;
  }
  return true;
}

bool e2_agent::handle_e2_rx_msg(srsran::unique_byte_buffer_t pdu,
                                const sockaddr_in&           from,
                                const sctp_sndrcvinfo&       sri,
                                int                          flags)
{
  printf("E2_AGENT: Received %d bytes from %s\n", pdu->N_bytes, inet_ntoa(from.sin_addr));
  e2_ap_pdu_c    pdu_c;
  asn1::cbit_ref bref(pdu->msg, pdu->N_bytes);
  if (pdu_c.unpack(bref) != asn1::SRSASN_SUCCESS) {
    logger.error("Failed to unpack RX E2 PDU");
    return false;
  }
  if (pdu_c.type().value == e2_ap_pdu_c::types_opts::init_msg) {
    logger.info("Received E2AP Init Message");
    handle_e2_init_msg(pdu_c.init_msg());
  } else if (pdu_c.type().value == e2_ap_pdu_c::types_opts::successful_outcome) {
    logger.info("Received E2AP Successful Outcome");
    handle_e2_successful_outcome(pdu_c.successful_outcome());
  } else if (pdu_c.type().value == e2_ap_pdu_c::types_opts::unsuccessful_outcome) {
    logger.info("Received E2AP Unsuccessful Outcome ");
    handle_e2_unsuccessful_outcome(pdu_c.unsuccessful_outcome());
  } else {
    logger.warning("Received E2AP Unknown Message ");
  }
  return true;
}

bool e2_agent::handle_e2_init_msg(asn1::e2ap::init_msg_s& init_msg)
{
  using namespace asn1::e2ap;
  if (init_msg.value.type() == e2_ap_elem_procs_o::init_msg_c::types_opts::ricsubscription_request) {
    logger.info("Received E2AP RIC Subscription Request");
    handle_ric_subscription_request(init_msg.value.ricsubscription_request());
  } else if (init_msg.value.type() == e2_ap_elem_procs_o::init_msg_c::types_opts::ricsubscription_delete_request) {
    logger.info("Received E2AP RIC Subscription Delete Request");
    handle_ric_subscription_delete_request(init_msg.value.ricsubscription_delete_request());
  } else if (init_msg.value.type() == e2_ap_elem_procs_o::init_msg_c::types_opts::ri_cctrl_request) {
    logger.info("Received E2AP RIC Control Request");
    // handle_ri_cctrl_request(init_msg.value.ri_cctrl_request());
  } else if (init_msg.value.type() == e2_ap_elem_procs_o::init_msg_c::types_opts::e2conn_upd) {
    logger.info("Received E2AP E2 Connection Update");
    //handle_e2conn_upd(init_msg.value.e2conn_upd());
  } else if (init_msg.value.type() == e2_ap_elem_procs_o::init_msg_c::types_opts::reset_request) {
    logger.info("Received E2AP E2 Reset Request");
    handle_reset_request(init_msg.value.reset_request());
  } else if (init_msg.value.type() == e2_ap_elem_procs_o::init_msg_c::types_opts::e2_removal_request) {
    logger.info("Received E2AP E2 Removal Request");
    //handle_e2_removal_request(init_msg.value.e2_removal_request());
  } else {
    logger.warning("Received E2AP Unknown Init Message ");
  }
  // TODO check for different type of RIC generated init messages
  // eg. RIC subscription request, RIC Reset request, RIC control request, RIC subscription delete request
  return true;
}

bool e2_agent::handle_e2_successful_outcome(asn1::e2ap::successful_outcome_s& successful_outcome)
{
  using namespace asn1::e2ap;
  if (successful_outcome.value.type() == e2_ap_elem_procs_o::successful_outcome_c::types_opts::e2setup_resp) {
    logger.info("Received E2AP E2 Setup Response");
    handle_e2_setup_response(successful_outcome.value.e2setup_resp());
  } else if (successful_outcome.value.type() ==
             e2_ap_elem_procs_o::successful_outcome_c::types_opts::ricsubscription_resp) {
    logger.info("Received E2AP RIC Subscription Response");
    // handle_ric_subscription_response(successful_outcome.value.ric_subscription());
  } else if (successful_outcome.value.type() == e2_ap_elem_procs_o::successful_outcome_c::types_opts::ri_cctrl_ack) {
    logger.info("Received E2AP RIC Control acknowlegement  \n");
    // handle_ric_control_response(successful_outcome.value.ric_control());
  } else if (successful_outcome.value.type() ==
             e2_ap_elem_procs_o::successful_outcome_c::types_opts::ricservice_upd_ack) {
    logger.info("Received E2AP RIC Service Update acknowlegement \n");
    // handle_ric_service_update_ack(successful_outcome.value.ric_service_update());
  } else if (successful_outcome.value.type() ==
             e2_ap_elem_procs_o::successful_outcome_c::types_opts::ricsubscription_delete_resp) {
    logger.info("Received E2AP RIC Subscription Delete Response \n");
    // handle_ric_subscription_delete_response(successful_outcome.value.ric_subscription_delete());
  } else if (successful_outcome.value.type() == e2_ap_elem_procs_o::successful_outcome_c::types_opts::reset_resp) {
    logger.info("Received E2AP RIC Reset Response \n");
    handle_reset_response(successful_outcome.value.reset_resp());
  } else {
    logger.info("Received E2AP Unknown Successful Outcome \n");
  }
  return true;
}

bool e2_agent::handle_e2_setup_response(e2setup_resp_s setup_response)
{
  if (e2ap_.process_setup_response(setup_response)) {
    logger.error("Failed to process E2 Setup Response \n");
    return false;
  }
  return true;
}

bool e2_agent::handle_e2_unsuccessful_outcome(asn1::e2ap::unsuccessful_outcome_s& unsuccessful_outcome)
{
  using namespace asn1::e2ap;
  if (unsuccessful_outcome.value.type() == e2_ap_elem_procs_o::unsuccessful_outcome_c::types_opts::e2setup_fail) {
    logger.info("Received E2AP E2 Setup Failure");
    if (e2ap_.process_e2_setup_failure(unsuccessful_outcome.value.e2setup_fail())) {
      logger.error("Failed to process E2 Setup Failure \n");
      return false;
    }
  } else if (unsuccessful_outcome.value.type() ==
             e2_ap_elem_procs_o::unsuccessful_outcome_c::types_opts::e2node_cfg_upd_fail) {
    logger.info("Received E2node configuration update Failure");
    if (e2ap_.process_e2_node_config_update_failure(unsuccessful_outcome.value.e2node_cfg_upd_fail())) {
      logger.error("Failed to process E2node configuration update Failure \n");
      return false;
    }
  } else if (unsuccessful_outcome.value.type() ==
             e2_ap_elem_procs_o::unsuccessful_outcome_c::types_opts::ricservice_upd_fail) {
    logger.info("Received E2AP RIC Service Update Failure \n");
    if (e2ap_.process_ric_service_update_failure(unsuccessful_outcome.value.ricservice_upd_fail())) {
      logger.error("Failed to process RIC service update failure \n");
      return false;
    }
  } else if (unsuccessful_outcome.value.type() ==
             e2_ap_elem_procs_o::unsuccessful_outcome_c::types_opts::e2_removal_fail) {
    logger.info("Received E2AP removal Unsuccessful Outcome \n");
    if (e2ap_.process_e2_removal_failure(unsuccessful_outcome.value.e2_removal_fail())) {
      logger.error("Failed to process RIC service status failure \n");
      return false;
    }
  } else {
    logger.info("Received E2AP Unknown Unsuccessful Outcome \n");
  }

  return true;
}

bool e2_agent::handle_ric_subscription_request(ricsubscription_request_s ric_subscription_request)
{
  logger.info("Received RIC Subscription Request from RIC ID: %i (instance id %i) to RAN Function ID: %i",
              ric_subscription_request->ri_crequest_id->ric_requestor_id,
              ric_subscription_request->ri_crequest_id->ric_instance_id,
              ric_subscription_request->ra_nfunction_id->value);

  if (e2ap_.process_subscription_request(ric_subscription_request)) {
    logger.error("Failed to process RIC subscription request \n");
    return false;
  }

  return true;
}

bool e2_agent::handle_ric_subscription_delete_request(ricsubscription_delete_request_s ricsubscription_delete_request)
{
  logger.info("Received RIC Subscription Delete request from RIC ID: %i (instance id %i) to RAN Function ID: %i",
              ricsubscription_delete_request->ri_crequest_id->ric_requestor_id,
              ricsubscription_delete_request->ri_crequest_id->ric_instance_id,
              ricsubscription_delete_request->ra_nfunction_id->value);

  if (e2ap_.process_subscription_delete_request(ricsubscription_delete_request)) {
    logger.error("Failed to process RIC subscription delete request \n");
    return false;
  }

  return true;
}

bool e2_agent::handle_subscription_modification_request(uint32_t ric_subscription_modification_request)
{
  if (e2ap_.process_subscription_modification_request(ric_subscription_modification_request)) {
    logger.error("Failed to process RIC subscription delete request \n");
    return false;
  }
  return true;
}
bool e2_agent::handle_subscription_modification_confirm(uint32_t ric_subscription_modification_confirm)
{
  if (e2ap_.process_subscription_modification_confirm(ric_subscription_modification_confirm)) {
    logger.error("Failed to process RIC subscription delete request \n");
    return false;
  }
  return true;
}
bool e2_agent::handle_subscription_modification_refuse(uint32_t ric_subscription_modification_refuse)
{
  if (e2ap_.process_subscription_modification_refuse(ric_subscription_modification_refuse)) {
    logger.error("Failed to process RIC subscription delete request \n");
    return false;
  }
  return true;
}

bool e2_agent::handle_reset_request(reset_request_s& reset_request)
{
  printf("Received E2AP E2 Reset Request \n");
  // call process to handle reset request, if it fails log error and return false, else return true - success
  if (e2ap_.process_reset_request(reset_request)) {
    logger.error("Failed to process E2 Reset Request \n");
    return false;
  }

  logger.info("Reset transaction with ID = {}", e2ap_.get_reset_id());

  // send reset reset response
  auto send_reset_resp = [this]() { send_e2_msg(E2_RESET_RESPONSE); };
  ric_rece_task_queue.push(send_reset_resp);

  return true;
}

bool e2_agent::handle_reset_response(reset_resp_s& reset_response)
{
  printf("Received E2AP E2 Reset Response \n");
  // call process to handle reset reponse, if it fails log error, else return true - success
  // all processing of message will be done in process_reset_response (e2ap.cc)
  if (e2ap_.process_reset_response(reset_response)) {
    logger.error("Failed to process E2 Reset Response \n");
    return false;
  }

  logger.info("Reset Response successfully processed \n");

  return true;
}
