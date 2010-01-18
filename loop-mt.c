#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <net_helper.h>
#include <topmanager.h>

#include "streaming.h"
#include "loop.h"

static int chunks_per_period = 1;
static int period = 500000;
static int done;
static pthread_mutex_t cb_mutex;
static pthread_mutex_t topology_mutex;
static struct nodeID *s;

static void *chunk_forging(void *dummy)
{
  int chunk_period = period / chunks_per_period;

  while(!done) {
    usleep(chunk_period);
    pthread_mutex_lock(&cb_mutex);
    generated_chunk();
    pthread_mutex_unlock(&cb_mutex);
  }

  return NULL;
}

static void *source_receive(void *dummy)
{
  while (!done) {
    int len;
    struct nodeID *remote;
#define BUFFSIZE 1024
  static uint8_t buff[BUFFSIZE];

    len = recv_data(s, &remote, buff, BUFFSIZE);
    switch (buff[0] /* Message Type */) {
      case 0x10 /* NCAST_PROTO */:
        pthread_mutex_lock(&topology_mutex);
        topParseData(buff, len);
        pthread_mutex_unlock(&topology_mutex);
        break;
      default:
        fprintf(stderr, "Unknown Message Type %x\n", buff[0]);
    }
    free(remote);
  }

  return NULL;
}

static void *receive(void *dummy)
{
  while (!done) {
    int len;
    struct nodeID *remote;
#define BUFFSIZE 1024
  static uint8_t buff[BUFFSIZE];

    len = recv_data(s, &remote, buff, BUFFSIZE);
    switch (buff[0] /* Message Type */) {
      case 0x10 /* NCAST_PROTO */:
        pthread_mutex_lock(&topology_mutex);
        topParseData(buff, len);
        pthread_mutex_unlock(&topology_mutex);
        break;
      case 12:
        pthread_mutex_lock(&cb_mutex);
        received_chunk(buff, len);
        pthread_mutex_unlock(&cb_mutex);
      default:
        fprintf(stderr, "Unknown Message Type %x\n", buff[0]);
    }
    free(remote);
  }

  return NULL;
}

static void *topology_sending(void *dummy)
{
  int gossiping_period = period * 10;

  pthread_mutex_lock(&topology_mutex);
  topParseData(NULL, 0);
  pthread_mutex_unlock(&topology_mutex);
  while(!done) {
    usleep(gossiping_period);
    pthread_mutex_lock(&topology_mutex);
    topParseData(NULL, 0);
    pthread_mutex_unlock(&topology_mutex);
  }

  return NULL;
}

static void *chunk_sending(void *dummy)
{
  int chunk_period = period;

  while(!done) {
    const struct nodeID **neighbours;
    int n;

    usleep(chunk_period);
    pthread_mutex_lock(&topology_mutex);
    neighbours = topGetNeighbourhood(&n);
    pthread_mutex_lock(&cb_mutex);
    send_chunk(neighbours, n);
    pthread_mutex_unlock(&cb_mutex);
    pthread_mutex_unlock(&topology_mutex);
  }

  return NULL;
}

void loop(struct nodeID *s1, int csize)
{
  pthread_t receive_thread, gossiping_thread, distributing_thread;
  
  period = csize;
  s = s1;
 
  stream_init(8);	// FIXME!
  pthread_mutex_init(&cb_mutex, NULL);
  pthread_mutex_init(&topology_mutex, NULL);
  pthread_create(&receive_thread, NULL, receive, NULL); 
  pthread_create(&gossiping_thread, NULL, topology_sending, NULL); 
  pthread_create(&distributing_thread, NULL, chunk_sending, NULL); 

  pthread_join(receive_thread, NULL);
  pthread_join(gossiping_thread, NULL);
  pthread_join(distributing_thread, NULL);
}

void source_loop(struct nodeID *s1, int csize, int chunks)
{
  pthread_t generate_thread, receive_thread, gossiping_thread, distributing_thread;
  
  period = csize;
  chunks_per_period = chunks;
  s = s1;
 
  stream_init(1);	// FIXME!
  pthread_mutex_init(&cb_mutex, NULL);
  pthread_mutex_init(&topology_mutex, NULL);
  pthread_create(&receive_thread, NULL, source_receive, NULL); 
  pthread_create(&gossiping_thread, NULL, topology_sending, NULL); 
  pthread_create(&generate_thread, NULL, chunk_forging, NULL); 
  pthread_create(&distributing_thread, NULL, chunk_sending, NULL); 

  pthread_join(generate_thread, NULL);
  pthread_join(receive_thread, NULL);
  pthread_join(gossiping_thread, NULL);
  pthread_join(distributing_thread, NULL);
}
