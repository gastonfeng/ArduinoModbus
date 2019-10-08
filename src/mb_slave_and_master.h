/*
 * Copyright (c) 2002,2016 Mario de Sousa (msousa@fe.up.pt)
 *
 * This file is part of the Modbus library for Beremiz and matiec.
 *
 * This Modbus library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this Modbus library.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This code is made available on the understanding that it will not be
 * used in safety-critical situations without a full and competent review.
 */




// #include "mb_slave.h"
// #include "mb_master.h"
#include "mb_util.h"


/************************************************************************

	initialise / shutdown the library

	These functions sets up/shut down the library state
        (allocate memory for buffers, initialise data strcutures, etc)

**************************************************************************/

#define DEF_CLOSE_ON_SILENCE 1    /* Used only by master nodes.
                                   * Flag indicating whether, by default, the connection
                                   * to the slave device should be closed whenever the
                                   * modbus_tcp_silence_init() function is called.
                                   *
                                   * 0  -> do not close connection
                                   * >0 -> close connection
                                   *
                                   * The spec sugests that connections that will not
                                   * be used for longer than 1 second should be closed.
                                   * Even though we expect most connections to have
                                   * silence intervals much shorted than 1 second, we
                                   * decide to use the default of shuting down the
                                   * connections because it is safer, and most other
                                   * implementations seem to do the same.
                                   * If we do not close we risk using up all the possible
                                   * connections that the slave can simultaneouly handle,
                                   * effectively locking out every other master that
                                   * wishes to communicate with that same slave.*/

typedef struct {
  int (*read_inbits)   (void *arg, uint16_t start_addr, uint16_t bit_count,  uint8_t  *data_bytes); /* bits are packed into bytes... */
  int (*read_outbits)  (void *arg, uint16_t start_addr, uint16_t bit_count,  uint8_t  *data_bytes); /* bits are packed into bytes... */
  int (*write_outbits) (void *arg, uint16_t start_addr, uint16_t bit_count,  uint8_t  *data_bytes); /* bits are packed into bytes... */
  int (*read_inwords)  (void *arg, uint16_t start_addr, uint16_t word_count, uint16_t *data_words);
  int (*read_outwords) (void *arg, uint16_t start_addr, uint16_t word_count, uint16_t *data_words);
  int (*write_outwords)(void *arg, uint16_t start_addr, uint16_t word_count, uint16_t *data_words);
  void *arg;
 } mb_slave_callback_t;


int mb_slave_and_master_init(int nd_count_tcp, int nd_count_rtu, int nd_count_ascii);

int mb_slave_and_master_done(void);

