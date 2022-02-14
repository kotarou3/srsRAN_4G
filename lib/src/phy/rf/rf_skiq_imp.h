/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include <stdbool.h>
#include <stdint.h>

#include "srsran/config.h"
#include "srsran/phy/rf/rf.h"

extern rf_dev_t srsran_rf_dev_skiq;

SRSRAN_API int rf_skiq_open(char* args, void** handler);

SRSRAN_API int rf_skiq_open_multi(char* args, void** handler, uint32_t nof_rx_antennas);

SRSRAN_API const char* rf_skiq_devname(void* h);

SRSRAN_API int rf_skiq_close(void* h);

SRSRAN_API int rf_skiq_start_rx_stream(void* h, bool now);

SRSRAN_API int rf_skiq_start_rx_stream_nsamples(void* h, uint32_t nsamples);

SRSRAN_API int rf_skiq_stop_rx_stream(void* h);

SRSRAN_API void rf_skiq_flush_buffer(void* h);

SRSRAN_API bool rf_skiq_has_rssi(void* h);

SRSRAN_API float rf_skiq_get_rssi(void* h);

SRSRAN_API void rf_skiq_set_master_clock_rate(void* h, double rate);

SRSRAN_API bool rf_skiq_is_master_clock_dynamic(void* h);

SRSRAN_API double rf_skiq_set_rx_srate(void* h, double freq);

SRSRAN_API int rf_skiq_set_rx_gain(void* h, double gain);

SRSRAN_API int rf_skiq_set_rx_gain_ch(void* h_, uint32_t ch, double rx_gain);

SRSRAN_API double rf_skiq_get_rx_gain(void* h);

SRSRAN_API double rf_skiq_get_tx_gain(void* h);

SRSRAN_API srsran_rf_info_t* rf_skiq_get_info(void* h);

SRSRAN_API void rf_skiq_suppress_stdout(void* h);

SRSRAN_API void rf_skiq_register_error_handler(void* h, srsran_rf_error_handler_t error_handler, void* arg);

SRSRAN_API double rf_skiq_set_rx_freq(void* h, uint32_t ch, double freq);

SRSRAN_API int
rf_skiq_recv_with_time(void* h, void* data, uint32_t nsamples, bool blocking, time_t* secs, double* frac_secs);

SRSRAN_API int
rf_skiq_recv_with_time_multi(void* h, void** data, uint32_t nsamples, bool blocking, time_t* secs, double* frac_secs);

SRSRAN_API double rf_skiq_set_tx_srate(void* h, double freq);

SRSRAN_API int rf_skiq_set_tx_gain(void* h, double gain);

SRSRAN_API int rf_skiq_set_tx_gain_ch(void* h, uint32_t ch, double gain);

SRSRAN_API double rf_skiq_set_tx_freq(void* h, uint32_t ch, double freq);

SRSRAN_API void rf_skiq_get_time(void* h, time_t* secs, double* frac_secs);

SRSRAN_API int rf_skiq_send_timed(void*  h,
                                  void*  data,
                                  int    nsamples,
                                  time_t secs,
                                  double frac_secs,
                                  bool   has_time_spec,
                                  bool   blocking,
                                  bool   is_start_of_burst,
                                  bool   is_end_of_burst);

SRSRAN_API int rf_skiq_send_timed_multi(void*  h,
                                        void*  data[4],
                                        int    nsamples,
                                        time_t secs,
                                        double frac_secs,
                                        bool   has_time_spec,
                                        bool   blocking,
                                        bool   is_start_of_burst,
                                        bool   is_end_of_burst);
