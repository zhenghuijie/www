/*-------------------------------------------------------------------------*/
/**
   @file    dictionary.c
   @author  N. Devillard
   @brief   Implements a dictionary for string variables.

   This module implements a simple dictionary object, i.e. a list
   of string/string associations. This object is useful to store e.g.
   informations retrieved from a configuration file (ini files).
*/
/*--------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
                                Includes
 ---------------------------------------------------------------------------*/
#include "dictionary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "util.h"

/** Maximum value size for integers and doubles. */
#define MAXVALSZ    1024

/** Minimal allocated number of entries in a dictionary */
#define DICTMINSZ   128

/** Invalid key token */
#define DICT_INVALID_KEY    ((char*)-1)

/*---------------------------------------------------------------------------
                            Private functions
 ---------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/
/**
  @brief    Double the size of the dictionary
  @param    d Dictionary to grow
  @return   This function returns non-zero in case of failure
 */
/*--------------------------------------------------------------------------*/
static int dictionary_grow(dictionary * d)
{
    char        ** new_val ;
    char        ** new_key ;
    unsigned     * new_hash ;
    int          * new_repeat ;

    new_val  = (char**) calloc(d->size * 2, sizeof *d->val);
    new_key  = (char**) calloc(d->size * 2, sizeof *d->key);
    new_hash = (unsigned*) calloc(d->size * 2, sizeof *d->hash);
    new_repeat = (int*) calloc(d->size * 2, sizeof *d->repeat);
    if (!new_val || !new_key || !new_hash || !new_repeat) {
        /* An allocation failed, leave the dictionary unchanged */
        if (new_val)
            free(new_val);
        if (new_key)
            free(new_key);
        if (new_hash)
            free(new_hash);
        if(new_repeat)
        	free(new_repeat);
        return -1 ;
    }
    /* Initialize the newly allocated space */
    memcpy(new_val, d->val, d->size * sizeof(char *));
    memcpy(new_key, d->key, d->size * sizeof(char *));
    memcpy(new_hash, d->hash, d->size * sizeof(unsigned));
    memcpy(new_repeat, d->repeat, d->size * sizeof(int));
    /* Delete previous data */
    free(d->val);
    free(d->key);
    free(d->hash);
    free(d->repeat);
    /* Actually update the dictionary */
    d->size *= 2 ;
    d->val = new_val;
    d->key = new_key;
    d->hash = new_hash;
    d->repeat = new_repeat;
    return 0 ;
}

/*---------------------------------------------------------------------------
                            Function codes
 ---------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------*/
/**
  @brief    Compute the hash key for a string.
  @param    key     Character string to use for key.
  @return   1 unsigned int on at least 32 bits.

  This hash function has been taken from an Article in Dr Dobbs Journal.
  This is normally a collision-free function, distributing keys evenly.
  The key is stored anyway in the struct so that collision can be avoided
  by comparing the key itself in last resort.
 */
/*--------------------------------------------------------------------------*/
unsigned dictionary_hash(const char * key)
{
    size_t      len ;
    unsigned    hash ;
    size_t      i ;

    if (!key)
        return 0 ;

    len = strlen(key);
    for (hash=0, i=0 ; i<len ; i++) {
        hash += (unsigned)key[i] ;
        hash += (hash<<10);
        hash ^= (hash>>6) ;
    }
    hash += (hash <<3);
    hash ^= (hash >>11);
    hash += (hash <<15);
    return hash ;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Create a new dictionary object.
  @param    size    Optional initial size of the dictionary.
  @return   1 newly allocated dictionary objet.

  This function allocates a new dictionary object of given size and returns
  it. If you do not know in advance (roughly) the number of entries in the
  dictionary, give size=0.
 */
/*-------------------------------------------------------------------------*/
dictionary * dictionary_new(size_t size)
{
    dictionary  *   d ;

    /* If no size was specified, allocate space for DICTMINSZ */
    if (size<DICTMINSZ) size=DICTMINSZ ;

    d = (dictionary*) calloc(1, sizeof *d) ;

    if (d) {
    	d->is_comment_less = 1;
        d->size = size ;
        d->val  = (char**) calloc(size, sizeof *d->val);
        d->key  = (char**) calloc(size, sizeof *d->key);
        d->hash = (unsigned*) calloc(size, sizeof *d->hash);
        d->repeat = (int*) calloc(size, sizeof *d->repeat);
        d->error_buf = buffer_init();
    }
    return d ;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Delete a dictionary object
  @param    d   dictionary object to deallocate.
  @return   void

  Deallocate a dictionary object and all memory associated to it.
 */
/*--------------------------------------------------------------------------*/
void dictionary_del(dictionary * d)
{
    ssize_t  i ;

    if (d==NULL) return ;
    for (i=0 ; i<d->size ; i++) {
        if (d->key[i]!=NULL)
            free(d->key[i]);
        if (d->val[i]!=NULL)
            free(d->val[i]);
    }
    free(d->val);
    free(d->key);
    free(d->hash);
    free(d->repeat);
    buffer_free(d->error_buf);
    free(d);
    return ;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Get a value from a dictionary.
  @param    d       dictionary object to search.
  @param    key     Key to look for in the dictionary.
  @param    def     Default value to return if key not found.
  @return   1 pointer to internally allocated character string.

  This function locates a key in a dictionary and returns a pointer to its
  value, or the passed 'def' pointer if no such key can be found in
  dictionary. The returned character pointer points to data internal to the
  dictionary object, you should not try to free it or modify it.
 */
/*--------------------------------------------------------------------------*/
const char * dictionary_get(const dictionary * d, const char * key, const char * def)
{
    unsigned    hash ;
    ssize_t      i ;
    char tmp     [(1024 * 2) + 1] = {0}; /*1024 form iniparser.c macro ASCIILINESZ*/

    strlwc(key, tmp, sizeof(tmp));
    hash = dictionary_hash(tmp);
    for (i=0 ; i<d->size ; i++) {
        if (d->key[i]==NULL)
            continue ;
        /* Compare hash */
        if (hash==d->hash[i]) {
            /* Compare string, to avoid hash collisions */
            if (!strcasecmp(key, d->key[i])) {
                return d->val[i] ;
            }
        }
    }
    return def ;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Set a value in a dictionary.
  @param    d       dictionary object to modify.
  @param    key     Key to modify or add.
  @param    val     Value to add.
  @return   int     0 if Ok, anything else otherwise

  If the given key is found in the dictionary, the associated value is
  replaced by the provided one. If the key cannot be found in the
  dictionary, it is added to it.

  It is Ok to provide a NULL value for val, but NULL values for the dictionary
  or the key are considered as errors: the function will return immediately
  in such a case.

  Notice that if you dictionary_set a variable to NULL, a call to
  dictionary_get will return a NULL value: the variable will be found, and
  its value (NULL) is returned. In other words, setting the variable
  content to NULL is equivalent to deleting the variable from the
  dictionary. It is not possible (in this implementation) to have a key in
  the dictionary without value.

  This function returns non-zero in case of failure.
 */
/*--------------------------------------------------------------------------*/
int dictionary_set(dictionary * d, const char * key, const char * val)
{
    ssize_t         i ;
    unsigned       hash ;

    char tmp     [(1024 * 2) + 1] = {0}; /*1024 form iniparser.c macro ASCIILINESZ*/

    if (d==NULL || key==NULL) return -1 ;

    strlwc(key, tmp, sizeof(tmp));
    /* Compute hash for this key */
    hash = dictionary_hash(tmp) ;
    /* Find if value is already in dictionary */
    if (d->n>0) {
        for (i=0 ; i<d->size ; i++) {
            if (d->key[i]==NULL)
                continue ;
            if (hash==d->hash[i]) { /* Same hash value */
                if (!strcasecmp(key, d->key[i])) {   /* Same key */
                    /* Found a value: modify and return */
                    if (d->val[i]!=NULL)
                        free(d->val[i]);
                    d->val[i] = (val ? xstrdup(val) : NULL);
                    d->repeat[i]++;
                    /* Value has been modified: return */
                    return 0 ;
                }
            }
        }
    }
    /* Add a new value */
    /* See if dictionary needs to grow */
    if (d->n==d->size) {
        /* Reached maximum size: reallocate dictionary */
        if (dictionary_grow(d) != 0)
            return -1;
    }

    /* Insert key in the first empty slot. Start at d->n and wrap at
       d->size. Because d->n < d->size this will necessarily
       terminate. */
    for (i=d->n ; d->key[i] ; ) {
        if(++i == d->size) i = 0;
    }
    /* Copy key */
    d->key[i]  = xstrdup(key);
    d->val[i]  = (val ? xstrdup(val) : NULL) ;
    d->hash[i] = hash;
    d->repeat[i]++;
    d->n ++ ;
    return 0 ;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Delete a key in a dictionary
  @param    d       dictionary object to modify.
  @param    key     Key to remove.
  @return   void

  This function deletes a key in a dictionary. Nothing is done if the
  key cannot be found.
 */
/*--------------------------------------------------------------------------*/
void dictionary_unset(dictionary * d, const char * key)
{
    unsigned    hash ;
    ssize_t      i ;
    char tmp     [(1024 * 2) + 1] = {0}; /*1024 form iniparser.c macro ASCIILINESZ*/

    if (key == NULL || d == NULL) {
        return;
    }

    strlwc(key, tmp, sizeof(tmp));
    hash = dictionary_hash(tmp);
    for (i=0 ; i<d->size ; i++) {
        if (d->key[i]==NULL)
            continue ;
        /* Compare hash */
        if (hash==d->hash[i]) {
            /* Compare string, to avoid hash collisions */
            if (!strcasecmp(key, d->key[i])) {
                /* Found key */
                break ;
            }
        }
    }
    if (i>=d->size)
        /* Key not found */
        return ;

    free(d->key[i]);
    d->key[i] = NULL ;
    if (d->val[i]!=NULL) {
        free(d->val[i]);
        d->val[i] = NULL ;
    }
    d->hash[i] = 0 ;
    d->repeat[i] = 0 ;
    d->n -- ;
    return ;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Dump a dictionary to an opened file pointer.
  @param    d   Dictionary to dump
  @param    f   Opened file pointer.
  @return   void

  Dumps a dictionary onto an opened file pointer. Key pairs are printed out
  as @c [Key]=[Value], one per line. It is Ok to provide stdout or stderr as
  output file pointers.
 */
/*--------------------------------------------------------------------------*/
void dictionary_dump(const dictionary * d, FILE * out)
{
    ssize_t  i ;

    if (d==NULL || out==NULL) return ;
    if (d->n<1) {
        fprintf(out, "empty dictionary\n");
        return ;
    }
    for (i=0 ; i<d->size ; i++) {
        if (d->key[i]) {
            fprintf(out, "%40s\t[%s]\t\t[repeat %d]\n",
                    d->key[i],
                    d->val[i] ? d->val[i] : "UNDEF", d->repeat[i]);
        }
    }
    return ;
}

void dictionary_report_dump(const dictionary * d, FILE * out)
{
    ssize_t  i ;
    int repeat = 0;
    bool is_report_repeat = false;
    json_t *json = json_new_object(),
    		*repeat_array_json = NULL;
    buffer *repeatBuf = buffer_init();

    if(json == NULL)
    {
    	return ;
    }
    if (d==NULL || out==NULL) return ;
    if (d->n<1) {
    	JSON_INSERT_STRING(json, "Result", "empty dictionary");
    	json_stream_output(out, json);
    	json_free_value(&json);
        return ;
    }

    json_insert_number(json, "Section Sum", d->n);

    for (i=0 ; i<d->n ; i++) {
    	 if(d->repeat[i] > 1)
    	 {
    		 if(repeat_array_json == NULL &&
    				 NULL == (repeat_array_json = json_new_object()))
    		 {
    			// JSON_INSERT_STRING(json, "Warning", "File has some repeating section, Next is the Repeat");
    		 }
    		 repeat++;
    		 if(repeat == 1)
    		 {
    			 buffer_append_string_len(repeatBuf, CONST_STR_LEN("repeatList: "));
    			 if(d->key[i][0] == ':')
    			 {
    				 buffer_append_string_len(repeatBuf, d->key[i] + 1, strlen(d->key[i] + 1));
    			 }
    			 else
    			 {
    				 buffer_append_string_len(repeatBuf, d->key[i], strlen(d->key[i]));
    			 }
    		 }
    		 else
    		 {
    			 if(d->key[i][0] == ':')
    			 {
    				 buffer_append_string_len(repeatBuf, d->key[i] + 1, strlen(d->key[i] + 1));
    			 }
    			 else
    			 {
    				 buffer_append_string_len(repeatBuf, d->key[i], strlen(d->key[i]));
    			 }
    		 }
    	 }
    }
    if(repeat > 0)
    {
    	JSON_INSERT_STRING(json, "repeatList", repeatBuf->ptr);
    }

    if(d->error_buf->used != 0)
    {
    	JSON_INSERT_STRING(json, "Error", d->error_buf->ptr);
    }
	json_stream_output(out, json);
	json_free_value(&json);
    return ;
ERROR:
	return;
}

/* Test code */
#ifdef TESTDIC
#define NVALS 20000
int main(int argc, char *argv[])
{
    dictionary  *   d ;
    char    *   val ;
    int         i ;
    char        cval[90] ;

    /* Allocate dictionary */
    printf("allocating...\n");
    d = dictionary_new(0);
    
    /* Set values in dictionary */
    printf("setting %d values...\n", NVALS);
    for (i=0 ; i<NVALS ; i++) {
        sprintf(cval, "%04d", i);
        dictionary_set(d, cval, "salut");
    }
    printf("getting %d values...\n", NVALS);
    for (i=0 ; i<NVALS ; i++) {
        sprintf(cval, "%04d", i);
        val = dictionary_get(d, cval, DICT_INVALID_KEY);
        if (val==DICT_INVALID_KEY) {
            printf("cannot get value for key [%s]\n", cval);
        }
    }
    printf("unsetting %d values...\n", NVALS);
    for (i=0 ; i<NVALS ; i++) {
        sprintf(cval, "%04d", i);
        dictionary_unset(d, cval);
    }
    if (d->n != 0) {
        printf("error deleting values\n");
    }
    printf("deallocating...\n");
    dictionary_del(d);
    return 0 ;
}
#endif
/* vim: set ts=4 et sw=4 tw=75 */
