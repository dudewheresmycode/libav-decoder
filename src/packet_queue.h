

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
} PacketQueue;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {


  if(av_dup_packet(pkt) < 0) {
    return -1;
  }
  AVPacketList *pkt1;
  pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;

  //SDL_LockMutex(q->mutex);

  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  //SDL_CondSignal(q->cond);

  //SDL_UnlockMutex(q->mutex);
  return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  AVPacketList *pkt1;
  int ret;

  //SDL_LockMutex(q->mutex);

  for(;;) {

    pkt1 = q->first_pkt;
    if (pkt1) {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt) q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    } else if (!block) {
      fprintf(stderr, "no block\n");
      ret = 0;
      break;
    } else {
      //fprintf(stderr, "sdl stop.. ?\n");
      // Sleep(100); //wait 100ms? no SDL_CondWait here, so i put a random sleep?...?
      ret = -1; //????
      break; //????
      //SDL_CondWait(q->cond, q->mutex);
    }
  }
  //SDL_UnlockMutex(q->mutex);
  return ret;
}
