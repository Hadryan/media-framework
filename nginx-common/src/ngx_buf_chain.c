#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_buf_chain.h"


ngx_buf_chain_t *
ngx_buf_chain_seek(ngx_buf_chain_t *head, size_t *offset)
{
    for ( ;; ) {

        if (*offset < head->size) {
            return head;
        }

        *offset -= head->size;
        head = head->next;
    }
}


ngx_int_t
ngx_buf_chain_skip(ngx_buf_chain_t **head_ptr, size_t size)
{
    ngx_buf_chain_t  *head = *head_ptr;

    while (size > 0) {

        if (head == NULL) {
            return NGX_ERROR;
        }

        if (size < head->size) {
            head->data += size;
            head->size -= size;
            break;
        }

        size -= head->size;
        head = head->next;
        *head_ptr = head;
    }

    return NGX_OK;
}


void *
ngx_buf_chain_copy(ngx_buf_chain_t **head_ptr, void *buf, size_t size)
{
    u_char           *p;
    ngx_buf_chain_t  *head = *head_ptr;

    p = buf;
    while (size > 0) {

        if (head == NULL) {
            return NULL;
        }

        if (size < head->size) {
            p = ngx_copy(p, head->data, size);
            head->data += size;
            head->size -= size;
            break;
        }

        p = ngx_copy(p, head->data, head->size);
        size -= head->size;
        head = head->next;
        *head_ptr = head;
    }

    return p;
}


ngx_int_t
ngx_buf_chain_compare(ngx_buf_chain_t *head, void *buf, size_t size)
{
    u_char     *p;
    ngx_int_t   rc;

    if (size <= 0) {
        return 0;
    }

    p = buf;
    for (; head != NULL; head = head->next) {

        if (size <= head->size) {
            return ngx_memcmp(p, head->data, size);
        }

        rc = ngx_memcmp(p, head->data, head->size);
        if (rc != 0) {
            return rc;
        }

        p += head->size;
        size -= head->size;
    }

    return 1;
}


ngx_buf_chain_t *
ngx_buf_chain_get_tail(ngx_buf_chain_t *data, uint32_t size)
{
    for ( ;; ) {

#if (NGX_DEBUG)
        if (size < data->size) {
            ngx_debug_point();
        }
#endif

        size -= data->size;
        if (size <= 0) {
            break;
        }

        data = data->next;
    }

    return data;
}


ngx_buf_chain_t *
ngx_buf_chain_terminate(ngx_buf_chain_t *data, uint32_t size)
{
    data = ngx_buf_chain_get_tail(data, size);

    data->next = NULL;

    return data;
}
