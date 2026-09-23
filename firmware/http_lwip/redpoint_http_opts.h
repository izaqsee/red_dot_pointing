#pragma once
// Include from the target's lwipopts.h. fs_close_custom releases a request slot
// once httpd queues the response, potentially BEFORE the last TCP ACK. Copy only
// RAM-backed API replies so queued packets/retransmissions cannot refer to a
// cleared or reused slot. Immutable ROM assets/error pages remain zero-copy.
#define HTTP_IS_DATA_VOLATILE(hs) \
  (((hs)->handle != NULL && (hs)->handle->pextension != NULL) ? TCP_WRITE_FLAG_COPY : 0)
