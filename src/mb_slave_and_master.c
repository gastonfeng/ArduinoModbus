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

// #include <fcntl.h>	/* File control definitions */
// #include <stdio.h>	/* Standard input/output */
// #include <string.h>
// #include <stdlib.h>
// #include <termio.h>	/* POSIX terminal control definitions */
// #include <sys/time.h>	/* Time structures for select() */
// #include <unistd.h>	/* POSIX Symbolic Constants */
// #include <errno.h>	/* Error definitions */

// #include "mb_layer1.h"
// #include "mb_slave_private.h"
// #include "mb_master_private.h"
// #include "mb_slave.h"
// #include "mb_master.h"
#include "lwip/sockets.h"
#include "mb_addr.h"
#include "mb_util.h"
//#define DEBUG 		/* uncomment to see the data sent and received */

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

// layer1_funct_ptr_t fptr_[4] = {
//   { /* WARNING: TCP functions MUST be the first, as we have this hardcoded in the code! */
//     /*          more specifically, in the get_ttyfd() macro in mb_slave.c               */
//     /*                             in the mb_slave_new() function in mb_slave.c         */
//     /*                             in the mb_master_connect() function in mb_master.c   */
//     &modbus_tcp_write
//    ,&modbus_tcp_read
//    ,&modbus_tcp_init
//    ,&modbus_tcp_done
//    ,&modbus_tcp_connect
//    ,&modbus_tcp_listen
//    ,&modbus_tcp_close
//    ,&modbus_tcp_silence_init
//    ,&modbus_tcp_get_min_timeout
//   },{
//     &modbus_rtu_write
//    ,&modbus_rtu_read
//    ,&modbus_rtu_init
//    ,&modbus_rtu_done
//    ,&modbus_rtu_connect
//    ,&modbus_rtu_listen
//    ,&modbus_rtu_close
//    ,&modbus_rtu_silence_init
//    ,&modbus_rtu_get_min_timeout
//   },{
//     &modbus_ascii_write
//    ,&modbus_ascii_read
//    ,&modbus_ascii_init
//    ,&modbus_ascii_done
//    ,&modbus_ascii_connect
//    ,&modbus_ascii_listen
//    ,&modbus_ascii_close
//    ,&modbus_ascii_silence_init
//    ,&modbus_ascii_get_min_timeout
//   },{
//     FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE
//   }
// };

/************************************************************************

	initialise / shutdown the library

	These functions sets up/shut down the library state
        (allocate memory for buffers, initialise data strcutures, etc)

**************************************************************************/
static optimization_t optimization_;
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define MB_MASTER_NODE 12
#define MB_LISTEN_NODE 14
#define MB_SLAVE_NODE  16
#define MB_FREE_NODE   18
typedef struct {
    int    node_type;          /*   What kind of use we are giving to this node...
                                *   If node_type == MB_MASTER_NODE
                                *      The node descriptor was initialised by the
                                *      modbus_connect() function.
                                *      The node descriptor is being used by a master
                                *      device, and the addr contains the address of the slave.
                                *      Remember that in this case fd may be >= 0 while
                                *      we have a valid connection, or it may be < 0 when
                                *      the connection needs to be reset.
                                *   If node_type == MB_LISTEN_NODE
                                *      The node descriptor was initialised by the
                                *      modbus_listen() function.
                                *      The node is merely used to accept() new connection
                                *      requests. The new slave connections will use another
                                *      node to transfer data.
                                *      In this case fd must be >= 0.
                                *      fd < 0 is an ilegal state and should never occur.
                                *   If node_type == MB_SLAVE_NODE
                                *      The node descriptor was initialised when a new
                                *      connection request arrived on a MB_LISTEN type node.
                                *      The node descriptor is being used by a slave device,
                                *      and is currently being used to connect to a master.
                                *      In this case fd must be >= 0.
                                *      fd < 0 is an ilegal state and should never occur.
                                *   If node_type == FREE_ND
                                *      The node descriptor is currently not being used.
                                *      In this case fd is set to -1, but is really irrelevant.
                                */
    int close_on_silence;      /* A flag used only by Master Nodes.
                                * When (close_on_silence > 0), then the connection to the
                                * slave device will be shut down whenever the
                                * modbus_tcp_silence_init() function is called.
                                * Remember that the connection will be automatically
                                * re-established the next time the user wishes to communicate
                                * with the same slave (using this same node descripto).
                                * If the user wishes to comply with the sugestion
                                * in the OpenModbus Spec, (s)he should set this flag
                                * if a silence interval longer than 1 second is expected.
                                */
    int print_connect_error;   /* flag to guarantee we only print an error the first time we
                                * attempt to connect to a emote server.
                                * Stops us from generting a cascade of errors while the slave
                                * is down.
                                * Flag will get reset every time we successfully
                                * establish a connection, so a message is once again generated 
                                * on the next error.
                                */
    uint8_t *recv_buf;              /* This node's receive buffer
                                * The library supports multiple simultaneous connections,
                                * and may need to receive multiple frames through mutiple nodes concurrently.
                                * To make the library thread-safe, we use one buffer for each node.
                                */
} nd_entry_t;
typedef struct {
      /* the array of node descriptors, and current size... */
    nd_entry_t *node;
    int        node_count;      /* total number of nodes in the node[] array */
} nd_table_t;

static nd_table_t nd_table_tcp = {.node=FALSE, .node_count=0};
static nd_table_t nd_table_rtu = {.node=FALSE, .node_count=0};
static nd_table_t nd_table_ascii = {.node=FALSE, .node_count=0};
static int buff_extra_bytes_;
int modbus_tcp_init_counter = 0;

int mb_slave_and_master_done(void)
{
  int res = 0;
  res |= mb_slave_done__();
  res |= mb_master_done__();
  res |= modbus_ascii_done();
  res |= modbus_rtu_done();
  res |= modbus_tcp_done();
  return res;
}
static inline void nd_entry_init(nd_entry_t *nde) {
  // nde->fd = -1; /* The node is free... */
}
static int nd_table_init(nd_table_t *ndt, int nd_count) {
  int count;

  if (ndt->node != NULL) {
    /* this function has already been called, and the node table is already initialised */
    return (ndt->node_count == nd_count)?0:-1;
  }

  /* initialise the node descriptor metadata array... */
  ndt->node = malloc(sizeof(nd_entry_t) * nd_count);
  if (ndt->node == NULL) {
#ifdef ERRMSG
    fprintf(stderr, ERRMSG_HEAD "Out of memory: error initializing node address buffer\n");
#endif
    return -1;
  }
  ndt->node_count = nd_count;

    /* initialise the state of each node in the array... */
  for (count = 0; count < ndt->node_count; count++) {
    nd_entry_init(&ndt->node[count]);
  } /* for() */

  return nd_count; /* number of succesfully created nodes! */
}
static int nd_entry_free(nd_entry_t *nde) {
  // if (nde->fd < 0)
    /* already free */
    // return -1;

  /* reset the tty device old settings... */
#ifdef ERRMSG
  int res =
#endif
            // tcsetattr(nde->fd, TCSANOW, &nde->old_tty_settings_);
#ifdef ERRMSG
  if(res < 0)
    fprintf(stderr, ERRMSG_HEAD "Error reconfiguring serial port to it's original settings.\n");
#endif

  // recv_buf_done(&nde->recv_buf_);
  // close(nde->fd);
  // nde->fd = -1;

  return 0;
}

int modbus_tcp_init(int nd_count,
                    optimization_t opt /* ignored... */,
                    int *extra_bytes)
{
#ifdef DEBUG
  printf("[%lu] modbus_tcp_init(): called...\n", pthread_self());
  printf("[%lu] creating %d nodes:\n", pthread_self(), nd_count);
#endif

  modbus_tcp_init_counter++;

  /* set the extra_bytes value... */
  /* Please see note before the modbus_rtu_write() function for a
     * better understanding of this extremely ugly hack... This will be
     * in the mb_rtu.c file!!
     *
     * The number of extra bytes that must be allocated to the data buffer
     * before calling modbus_tcp_write()
     */
  if (extra_bytes != FALSE)
    *extra_bytes = 0;

  if (0 == nd_count)
    /* no need to initialise this layer! */
    return 0;
  if (nd_count <= 0)
    /* invalid node count... */
    goto error_exit_1;

  /* initialise the node table... */
  if (nd_table_init(&nd_table_tcp, nd_count) < 0)
    goto error_exit_1;

#ifdef DEBUG
  printf("[%lu] modbus_tcp_init(): %d node(s) opened succesfully\n", pthread_self(), nd_count);
#endif
  return nd_count; /* number of succesfully created nodes! */

/*
error_exit_2:
  nd_table_done(&nd_table_);
*/
error_exit_1:
  if (extra_bytes != FALSE)
    *extra_bytes = 0;
  return -1;
}

int modbus_rtu_init(int nd_count,
                    optimization_t opt,
                    int *extra_bytes)
{
#ifdef DEBUG
  fprintf(stderr, "modbus_rtu_init(): called...\n");
  fprintf(stderr, "creating %d node descriptors\n", nd_count);
  if (opt == optimize_speed)
    fprintf(stderr, "optimizing for speed\n");
  if (opt == optimize_size)
    fprintf(stderr, "optimizing for size\n");
#endif

  /* check input parameters...*/
  if (0 == nd_count)
  {
    if (extra_bytes != FALSE)
      // Not the corect value for this layer.
      // What we set it to in case this layer is not used!
      *extra_bytes = 0;
    return 0;
  }
  if (nd_count <= 0)
    goto error_exit_0;

  if (extra_bytes == FALSE)
    goto error_exit_0;

//   if (crc_init(opt) < 0)
//   {
// #ifdef ERRMSG
//     fprintf(stderr, ERRMSG_HEAD "Out of memory: error initializing crc buffers\n");
// #endif
//     goto error_exit_0;
//   }

  /* set the extra_bytes value... */
  /* Please see note before the modbus_rtu_write() function for a
     * better understanding of this extremely ugly hack...
     *
     * The number of extra bytes that must be allocated to the data buffer
     * before calling modbus_rtu_write()
     */
  *extra_bytes = RTU_FRAME_CRC_LENGTH;

  /* initialise nd table... */
  if (nd_table_init(&nd_table_rtu, nd_count) < 0)
    goto error_exit_0;

  /* remember the optimization choice for later reference... */
  optimization_ = opt;

#ifdef DEBUG
  fprintf(stderr, "modbus_rtu_init(): returning succesfuly...\n");
#endif
  return 0;

error_exit_0:
  if (extra_bytes != FALSE)
    // Not the corect value for this layer.
    // What we set it to in case of error!
    *extra_bytes = 0;
  return -1;
}

int modbus_ascii_init(int nd_count,
                      optimization_t opt,
                      int *extra_bytes)
{
#ifdef DEBUG
  printf("modbus_asc_init(): called...\n");
  printf("creating %d node descriptors\n", nd_count);
  if (opt == optimize_speed)
    printf("optimizing for speed\n");
  if (opt == optimize_size)
    printf("optimizing for size\n");
#endif

  /* check input parameters...*/
  if (0 == nd_count)
  {
    if (extra_bytes != FALSE)
      // Not the corect value for this layer.
      // What we set it to in case this layer is not used!
      *extra_bytes = 0;
    return 0;
  }
  if (nd_count <= 0)
    goto error_exit_0;

  if (extra_bytes == FALSE)
    goto error_exit_0;

  /* initialise nd table... */
  if (nd_table_init(&nd_table_ascii, nd_count) < 0)
    goto error_exit_0;

  /* remember the optimization choice for later reference... */
  optimization_ = opt;

#ifdef DEBUG
  printf("modbus_asc_init(): returning succesfuly...\n");
#endif
  return 0;

error_exit_0:
  if (extra_bytes != FALSE)
    *extra_bytes = 0; // The value we set this to in case of error.
  return -1;
}

int mb_slave_init__(int extra_bytes)
{
  buff_extra_bytes_ = extra_bytes;
  return 0;
}

int mb_slave_done__(void)
{
  return 0;
}

int mb_master_init__(int extra_bytes)
{
#ifdef DEBUG
  fprintf(stderr, "mb_master_init__(extra_bytes=%d), QUERY_BUFFER_SIZE=%d\n", extra_bytes, QUERY_BUFFER_SIZE);
#endif
  buff_extra_bytes_ = extra_bytes;
  return 0;
}

/* Shut down the Modbus Master Layer */
int mb_master_done__(void)
{
  return 0;
}
static inline void nd_table_done(nd_table_t *ndt) {
  int i;

  if (ndt->node == NULL) 
    return;

    /* close all the connections... */
  for (i = 0; i < ndt->node_count; i++)
    nd_entry_free(&ndt->node[i]);

  /* Free memory... */
  free(ndt->node);
  *ndt = (nd_table_t){.node=NULL, .node_count=0};
}

int modbus_ascii_done(void) {
  nd_table_done(&nd_table_ascii);
  return 0;
}
int modbus_rtu_done(void) {
  nd_table_done(&nd_table_rtu);
  // crc_done();

  return 0;
}
int modbus_tcp_done(void) {
  int i;
  
  modbus_tcp_init_counter--;
  if (modbus_tcp_init_counter != 0) return 0; /* ignore this request */
  
    /* close all the connections... */
  for (i = 0; i < nd_table_tcp.node_count; i++)
    modbus_tcp_close(i);

  /* Free memory... */
  nd_table_done(&nd_table_tcp);

  return 0;
}
int mb_slave_and_master_init(int nd_count_tcp, int nd_count_rtu, int nd_count_ascii)
{
  int extra_bytes, extra_bytes_tcp, extra_bytes_rtu, extra_bytes_ascii;

#ifdef DEBUG
  fprintf(stderr, "mb_slave_and_master_init()\n");
  fprintf(stderr, "creating %d nodes\n", nd_count);
#endif

  /* initialise layer 1 library */
  if (modbus_tcp_init(nd_count_tcp, DEF_OPTIMIZATION, &extra_bytes_tcp) < 0)
    goto error_exit_0;
  if (modbus_rtu_init(nd_count_rtu, DEF_OPTIMIZATION, &extra_bytes_rtu) < 0)
    goto error_exit_1;
  if (modbus_ascii_init(nd_count_ascii, DEF_OPTIMIZATION, &extra_bytes_ascii) < 0)
    goto error_exit_2;
  extra_bytes = max(extra_bytes_tcp, extra_bytes_rtu);
  extra_bytes = max(extra_bytes, extra_bytes_ascii);

  /* initialise master and slave libraries... */
  if (mb_slave_init__(extra_bytes) < 0)
    goto error_exit_3;
  if (mb_master_init__(extra_bytes) < 0)
    goto error_exit_4;
  return 0;

/*
error_exit_3:
	modbus_master_done();
*/
error_exit_4:
  mb_slave_done__();
error_exit_3:
  modbus_ascii_done();
error_exit_2:
  modbus_rtu_done();
error_exit_1:
  modbus_tcp_done();
error_exit_0:
  return -1;
}

int modbus_tcp_close(int nd) {
#ifdef DEBUG
  fprintf(stderr, "[%lu] modbus_tcp_close(): called... nd=%d\n", pthread_self(), nd);
#endif

  if ((nd < 0) || (nd >= nd_table_tcp.node_count)) {
    /* invalid nd */
#ifdef DEBUG
    fprintf(stderr, "[%lu] modbus_tcp_close(): invalid node %d. Should be < %d\n", pthread_self(), nd, nd_table_.node_count);
#endif
    return -1;
  }

  if (nd_table_tcp.node[nd].node_type == MB_FREE_NODE)
    /* already free node */
    return 0;

  // close_connection(nd);

  // nd_table_close_node(&nd_table_tcp, nd);

  return 0;
}
int mb_slave_new(node_addr_t node_addr) {
  int res = -1;
  #ifdef DEBUG
  fprintf( stderr, "mb_slave_connect()\n");
  #endif

  /* call layer 1 library */
  switch(node_addr.naf) {
    case naf_tcp:  
      res = modbus_tcp_listen(node_addr);
      if (res >= 0) res = res*4 + 0 /* offset into fptr_ with TCP functions */;
      return res;
    // case naf_rtu:  
    //   res = modbus_rtu_listen(node_addr);
    //   if (res >= 0) res = res*4 + 1 /* offset into fptr_ with RTU functions */;
    //   return res;
    // case naf_ascii:  
    //   res = modbus_ascii_listen(node_addr);
    //   if (res >= 0) res = res*4 + 2 /* offset into fptr_ with ASCII functions */;
    //   return res;
  }

  return -1;
}




int mb_slave_close(int fd) {
  #ifdef DEBUG
  fprintf( stderr, "mb_slave_close(): nd = %d\n", fd);
  #endif
  // get_ttyfd(); /* declare the ttyfd variable!! */
  // /* call layer 1 library */
  // /* will call one of modbus_tcp_close(), modbus_rtu_close(), modbus_ascii_close() */
  // return modbus_close(ttyfd);
}


