/****************************************************************************
**
*W  precord.c                   GAP source                   Martin Schönert
**
**
*Y  Copyright (C)  1996,  Lehrstuhl D für Mathematik,  RWTH Aachen,  Germany
*Y  (C) 1998 School Math and Comp. Sci., University of St Andrews, Scotland
*Y  Copyright (C) 2002 The GAP Group
**
**  This file contains the functions for plain records.
**
**  A plain record  with <n>  components is stored  as  a bag  with 2*<n>+2
**  entries.  The odd entries are the record  names of the components and the
**  even entries are the corresponding values.
**  The first pointer in the bag is reserved for the pointer to the type
**  object if the record becomes a component object. The second slot
**  in the bag is the number <n> of components as a C int.
**  Normally, the rnams are sorted such that we can do a binary lookup.
**  However, we allow for lazy sorting. This is done as follows: New
**  components are always appended (record bags grow by a factor of 5/4
**  if needed as plists do), already sorted rnams are stored by storing
**  their negative value to indicate sortedness. The new entries will have
**  positive rnams and can thus be distinguished. Every read access will
**  clean up the mess by sorting the new part and then merging the two
**  sorted areas. After that, all rnams are negative indicating sortedness.
**
*/

#include "precord.h"

#include "ariths.h"
#include "bool.h"
#include "error.h"
#include "funcs.h"
#include "gaputils.h"
#include "io.h"
#include "modules.h"
#include "opers.h"
#include "plist.h"
#include "records.h"
#include "saveload.h"
#include "stringobj.h"

#ifdef HPCGAP
#include "hpc/aobjects.h"
#include "hpc/traverse.h"
#endif

/****************************************************************************
**
*F * * * * * * * * * standard functions for plain records * * * * * * * * * *
*/


/****************************************************************************
**
*F  TypePRec( <rec> ) . . . . . . . . . . . . . . . .  type of a plain record
**
**  'TypePRec' returns the type of the plain record <rec>.
**
**  'TypePRec' is the function in 'TypeObjFuncs' for plain records.
*/
Obj TYPE_PREC_MUTABLE;
Obj TYPE_PREC_IMMUTABLE;

Obj TypePRec(Obj prec)
{
    return IS_MUTABLE_PLAIN_OBJ(prec) ? TYPE_PREC_MUTABLE : TYPE_PREC_IMMUTABLE;
}

/****************************************************************************
**
*F  SetTypePRecToComObj( <rec>, <kind> )  convert record to component object
**
*/
void SetTypePRecToComObj( Obj rec, Obj kind )
{
    RetypeBag(rec, T_COMOBJ);
    SET_TYPE_COMOBJ(rec, kind);
    CHANGED_BAG(rec);
}

/****************************************************************************
**
*F  NEW_PREC( <len> ) . . . . . . . . . . . . . . . . make a new plain record
**
**  'NEW_PREC' returns a new plain record with room for <len> components,
**  already set to length len.
*/
Obj NEW_PREC(UInt len)
{
    Obj o = NewBag( T_PREC, (len) * 2*sizeof(Obj) + 2*sizeof(Obj) );
    SET_LEN_PREC(o,0);
    return o;
}

/****************************************************************************
**
*F  GrowPRec( <rec>, <len> ) . . . .  make sure <rec> can hold <len> components
**
**  Returns 0 if nothing changed and 1 if enlarged.
*/
Int             GrowPRec (
    Obj                 rec,
    UInt                need )
{
    UInt                newsize, want, good;

    /* check if big enough */
    want = (2*need+2)*sizeof(Obj);
    if (SIZE_OBJ(rec) >= want) return 0L;


    /* find out how large the bag should become at least                   */
    good = ((5 * LEN_PREC(rec) + 3)/4 + 1) * 2 * sizeof(Obj) + 2;

    /* but maybe we need more                                              */
    newsize = (want < good) ? good : want;

    /* resize the plain list                                               */
    ResizeBag( rec, newsize );

    return 1L;
}


#ifdef USE_THREADSAFE_COPYING
void TraversePRecord(Obj obj)
{
    UInt i, len = LEN_PREC(obj);
    for (i = 1; i <= len; i++)
        QueueForTraversal((Obj)GET_ELM_PREC(obj, i));
}

void CopyPRecord(Obj copy, Obj original)
{
    UInt i, len = LEN_PREC(original);
    for (i = 1; i <= len; i++)
        SET_ELM_PREC(copy, i, ReplaceByCopy(GET_ELM_PREC(original, i)));
}

#else

/****************************************************************************
**
*F  CopyPRec( <rec> ) . . . . . . . . . . . . . . . . . . copy a plain record
*F  CleanPRec( <rec> )  . . . . . . . . . . . . . . . clean up a plain record
**
**  'CopyPRec' returns a structural (deep) copy  of the record <rec>, i.e., a
**  recursive copy that preserves the structure.
**
**  If <rec>  has not yet  been  copied, it makes a   copy, leaves a  forward
**  pointer to the copy in  the first entry   of the record, where the  first
**  record name usually resides,  and copies all the  entries.  If the record
**  has alread been copied, it returns the value of the forwarding pointer.
**
**  'CopyPRec' is the function in 'TabCopyObj' for records.
**
**  'CleanPRec' removes the  mark and the forwarding  pointer from the record
**  <rec>.
**
**  'CleanPRec' is the function in 'TabCleanObj' for records.
*/
Obj CopyPRec (
    Obj                 rec,
    Int                 mut )
{
    Obj                 copy;           /* copy, result                    */
    Obj                 tmp;            /* temporary variable              */

    // immutable input is handled by COPY_OBJ
    GAP_ASSERT(IS_MUTABLE_OBJ(rec));

    /* make a copy                                                     */
    if ( mut ) {
        copy = NewBag( TNUM_OBJ(rec), SIZE_OBJ(rec) );
    }
    else {
        copy = NewBag( IMMUTABLE_TNUM(TNUM_OBJ(rec)), SIZE_OBJ(rec) );
    }
    memcpy(ADDR_OBJ(copy), CONST_ADDR_OBJ(rec), SIZE_OBJ(rec));

    // leave a forwarding pointer
    PrepareCopy(rec, copy);

    // copy the subvalues; since we used memcpy above, we don't need to worry
    // about copying the length or RNAMs; and by working solely inside the
    // copy, we avoid triggering tnum assertions in GET_ELM_PREC and
    // SET_ELM_PREC
    const UInt len = LEN_PREC(copy);
    for (UInt i = 1; i <= len; i++) {
        tmp = COPY_OBJ(GET_ELM_PREC(copy, i), mut);
        SET_ELM_PREC(copy, i, tmp);
        CHANGED_BAG(copy);
    }

    /* return the copy                                                     */
    return copy;
}

void CleanPRec (
    Obj                 rec )
{
    UInt                i;              /* loop variable                   */

    /* clean the subvalues                                             */
    for ( i = 1; i <= LEN_PREC(rec); i++ ) {
        CLEAN_OBJ( GET_ELM_PREC( rec, i ) );
    }
}

#endif //!defined(USE_THREADSAFE_COPYING)


/****************************************************************************
**
*F  MakeImmutablePRec( <rec> )
*/

void MakeImmutablePRec(Obj rec)
{
    // change the tnum first, to avoid infinite recursion for objects that
    // contain themselves
    RetypeBag(rec, IMMUTABLE_TNUM(TNUM_OBJ(rec)));

    // FIXME HPC-GAP: there is a potential race here: <rec> becomes public
    // the moment we change its type, but it's not ready for public access
    // until the following code completed.

    UInt len = LEN_PREC(rec);
    for (UInt i = 1; i <= len; i++)
        MakeImmutable(GET_ELM_PREC(rec, i));

    // Sort the record at this point. This can never hurt, unless the record
    // will never be accessed again anyway. But for HPC-GAP it is essential so
    // that immutable records are actually binary unchanging.
    SortPRecRNam(rec, 1);
}


/****************************************************************************
 * FindPRec( <rec>, <rnam>, <pos>, <cleanup> )
 *   . . . . . . . . . . . . . . . . . find a component name by binary search
 *
 * Searches rnam in rec, sets pos to the position where it is found (return
 * value 1) or where it should be inserted if it is not found (return val 0).
 * If cleanup is nonzero, a dirty record is automatically cleaned up.
 * If cleanup is 0, this does not happen.
 */

UInt FindPRec( Obj rec, UInt rnam, UInt *pos, int cleanup )
{
    /* This only assumes that the rnam values in the record are sorted! */
    UInt i;
    Int rnam2;
    UInt low = 1;
    UInt high;

    high = LEN_PREC(rec);
    if (high > 0 && (Int) (GET_RNAM_PREC(rec,high)) > 0) {
        /* DIRTY! Not everything sorted! */
        if (cleanup) {
            SortPRecRNam(rec,0);
            /* Note that this does not change the length and it cannot
             * trigger a garbage collection if cleanup is 1!
             * We do not want record accesses to trigger garbage
             * collections! */
        } else {
            /* We are not allowed to cleanup, so we live with it, we
             * first try to find rnam in the mess at the end, then
             * fall back to binary search: */
            i = high;
            while (i >= 1) {
                rnam2 = (Int) (GET_RNAM_PREC(rec,i));
                if (rnam == rnam2) {
                    *pos = i;
                    return 1;
                }
                if (rnam2 < 0) { /* reached the sorted area! */
                    high = i;  /* will be incremented by 1 */
                    break;
                }
                i--;
            }
            if (i == 0) return 0;
            /* Here, high is the number of the sorted entries. */
        }
    }
    high++;
    while (low < high) {
        i = (low + high) >> 1;   /* we always have low <= i < high */
        rnam2 = -(Int)(GET_RNAM_PREC( rec, i ));
        if (rnam2 < rnam) low = i+1;
        else if (rnam2 > rnam) high = i;
        else {
            /* found! */
            *pos = i;
            return 1;
        }
    }
    /* Now low == high and we did not find it. */
    *pos = low;
    return 0;
}

/****************************************************************************
**
*F  IsbPRec( <rec>, <rnam> )  . . . . test for an element from a plain record
**
**  'IsbPRec' returns 1 if the record <rec> has a component with  the  record
**  name <rnam>, and 0 otherwise.
*/
Int IsbPRec (
    Obj                 rec,
    UInt                rnam )
{
    UInt                i;              /* loop variable                   */

    return FindPRec(rec,rnam,&i,1);
}


/****************************************************************************
**
*F  ElmPRec( <rec>, <rnam> )  . . . . . select an element from a plain record
**
**  'ElmPRec' returns the element, i.e., the value of the component, with the
**  record name <rnam> in  the plain record <rec>.   An error is signalled if
**  <rec> has no component with record name <rnam>.
*/
Obj ElmPRec (
    Obj                 rec,
    UInt                rnam )
{
    UInt                i;              /* loop variable                   */

    if (FindPRec(rec,rnam,&i,1))
        return GET_ELM_PREC( rec, i );
    else {
        ErrorReturnVoid(
            "Record Element: '<rec>.%g' must have an assigned value",
            (Int)NAME_RNAM(rnam), 0L,
            "you can 'return;' after assigning a value" );
        return ELM_REC( rec, rnam );
    }
}


/****************************************************************************
**
*F  UnbPRec( <rec>, <rnam> )  . unbind a record component from a plain record
**
**  'UnbPRec'  removes the record component  with the record name <rnam> from
**  the record <rec>.
*/
void UnbPRec (
    Obj                 rec,
    UInt                rnam )
{
    UInt                len;            /* length of <rec>                 */
    UInt                i;              /* loop variable                   */

    // Accept T_PREC and T_COMOBJ, reject T_PREC+IMMUTABLE
    if (TNUM_OBJ(rec) == T_PREC+IMMUTABLE) {
        ErrorReturnVoid(
            "Record Unbind: <rec> must be a mutable record",
            0L, 0L,
            "you can 'return;' and ignore the unbind" );
    }

    if (FindPRec( rec, rnam, &i, 1 )) {
        /* otherwise move everything forward                               */
        len = LEN_PREC( rec );
        for ( ; i < len; i++ ) {
            SET_RNAM_PREC( rec, i, GET_RNAM_PREC( rec, i+1 ) );
            SET_ELM_PREC(  rec, i, GET_ELM_PREC(  rec, i+1 ) );
        }
        SET_RNAM_PREC( rec, len, 0 );
        SET_ELM_PREC( rec, len, 0L );


        /* resize the record                                               */
        SET_LEN_PREC(rec,LEN_PREC(rec)-1);

    } else
        /* do nothing if no such component exists                          */
        return;
}


/****************************************************************************
**
*F  AssPRec( <rec>, <rnam>, <val> ) . . . . . . . .  assign to a plain record
**
**  'AssPRec' assigns the value <val> to the record component with the record
**  name <rnam> in the plain record <rec>.
*/
void AssPRec (
    Obj                 rec,
    UInt                rnam,
    Obj                 val )
{
    UInt                len;            /* length of <rec>                 */
    UInt                i;              /* loop variable                   */

    // Accept T_PREC and T_COMOBJ, reject T_PREC+IMMUTABLE
    if (TNUM_OBJ(rec) == T_PREC+IMMUTABLE) {
        ErrorReturnVoid(
            "Record Assignment: <rec> must be a mutable record",
            0L, 0L,
            "you can 'return;' and ignore the assignment" );
    }

    /* get the length of the record                                        */
    len = LEN_PREC( rec );

    if (len % 1000 == 0) {   /* A hack to occasionally do some cleanup! */
        SortPRecRNam(rec,0);
    }

    if (!FindPRec( rec, rnam, &i, 0 )) {
        /* No cleanup allowed here to allow for multiple assignments! */
        /* extend the record if no such component exists                   */
        len++;
        GrowPRec(rec, len);
        i = len;
        SET_RNAM_PREC( rec, i, rnam );
        SET_LEN_PREC(rec,len);
    }
    /* assign the value to the component                                   */
    SET_ELM_PREC( rec, i, val );
    CHANGED_BAG( rec );
}

/****************************************************************************
**
*F  PrintPRec( <rec> )  . . . . . . . . . . . . . . . . . . .  print a record
**
**  'PrintRec' prints the plain record <rec>.
*/
extern Obj PrintObjOper;

void PrintPRec (
    Obj                 rec )
{
    DoOperation1Args( PrintObjOper, rec );
}



/****************************************************************************
**
*F  SortPRecRNam(<rec>, <inplace>) . . . . . . . sort the Rnams of the record
**
**  This is needed after the components of a record have been assigned
**  in not necessarily sorted order in the kernel. It is automatically
**  called on the first read access if necessary. See the top of "precord.c"
**  for a comment on lazy sorting.
**  If inplace is 1 then a slightly slower algorithm is used of
**  which we know that it does not produce garbage collections.
**  If inplace is 0 a garbage collection may be triggered.
**
*/
static int PrecComparer(const void *a, const void *b)
{
    const UInt *aa = (const UInt *) a;
    const UInt *bb = (const UInt *) b;
    if (*aa < *bb) return -1;
    else if (*aa == *bb) return 0;
    else return 1;
}

void SortPRecRNam (
    Obj                 rec, int inplace )
{
    UInt len = LEN_PREC(rec);
    UInt i,j,k,save;
    int issorted = 1;
    Obj space;
    Obj tmp;

    /* Nothing has to be done if it is already sorted: */
    if ( len == 0 || (Int) (GET_RNAM_PREC(rec,len)) < 0) return;

    /* First find the "unsorted part" and check whether it is sorted! */
    for (i = len-1;i >= 1 && (Int)(GET_RNAM_PREC(rec,i)) > 0;i--) {
        if (issorted && GET_RNAM_PREC(rec,i) > GET_RNAM_PREC(rec,i+1)) {
            issorted = 0;
        }
    }
    i++;
    /* Now i points to the last positive (and thus still unsorted)
     * component and issorted is 1 iff positions i to len are sorted! */
    if (!issorted && len - i + 1 > 1) {
        qsort( ADDR_OBJ(rec)+2*i, len-i+1, 2*sizeof(Obj), &PrecComparer );
    }
    /* Now i points to the first positive component and the rest is
     * sorted. */
    save = i;
    if (save == 1 ||
        -(Int)(GET_RNAM_PREC(rec,save-1)) < GET_RNAM_PREC(rec,save)) {
        /* Otherwise, nothing has to be done since it is already
         * sorted, we only have to negate the RNams! */
        for (j = save;j <= len;j++)
            SET_RNAM_PREC(rec,j,-(Int)(GET_RNAM_PREC(rec,j)));
        return;
    }
    /* Next we perform a merge sort on the two presorted areas. */
    /* For optimal performance, we need some space to mess around: */
    if (!inplace) {
        space = NEW_PREC(len);
        j = 1;
        k = 1;
        while (j < save && i <= len) {
            if (-(Int)(GET_RNAM_PREC(rec,j)) < GET_RNAM_PREC(rec,i)) {
                SET_RNAM_PREC(space,k,GET_RNAM_PREC(rec,j));
                SET_ELM_PREC(space,k,GET_ELM_PREC(rec,j));
                j++; k++;
            } else {
                SET_RNAM_PREC(space,k,-(Int)(GET_RNAM_PREC(rec,i)));
                SET_ELM_PREC(space,k,GET_ELM_PREC(rec,i));
                i++; k++;
            }
        }
        /* Copy the rest of the part still missing: */
        while (j < save) {
            SET_RNAM_PREC(space,k,GET_RNAM_PREC(rec,j));
            SET_ELM_PREC(space,k,GET_ELM_PREC(rec,j));
            j++; k++;
        }
        while (i <= len) {
            SET_RNAM_PREC(space,k,-(Int)(GET_RNAM_PREC(rec,i)));
            SET_ELM_PREC(space,k,GET_ELM_PREC(rec,i));
            i++; k++;
        }
        /* Finally, copy everything back to where it came from: */
        memcpy(ADDR_OBJ(rec)+2,CONST_ADDR_OBJ(space)+2,sizeof(Obj)*2*len);
    } else {   /* We have to work in place to avoid a garbage collection. */
        /* i == save is the cut point */
        j = 1;
        for (j = 1; j < save; j++) {
            if (-(Int)(GET_RNAM_PREC(rec,j)) > GET_RNAM_PREC(rec,i)) {
                /* we have to move something to position j! */
                tmp = (Obj) (-(Int)(GET_RNAM_PREC(rec,j)));
                SET_RNAM_PREC(rec,j,-(Int)(GET_RNAM_PREC(rec,i)));
                SET_RNAM_PREC(rec,i,(UInt) tmp);
                tmp = GET_ELM_PREC(rec,j);
                SET_ELM_PREC(rec,j,GET_ELM_PREC(rec,i));
                SET_ELM_PREC(rec,i,tmp);
                /* Now we have to "bubble pos i up" until it is in the
                 * right position: */
                for (k = i;k < len;k++) {
                    if (GET_RNAM_PREC(rec,k) > GET_RNAM_PREC(rec,k+1)) {
                        tmp = (Obj) GET_RNAM_PREC(rec,k);
                        SET_RNAM_PREC(rec,k,GET_RNAM_PREC(rec,k+1));
                        SET_RNAM_PREC(rec,k+1,(UInt) tmp);
                        tmp = GET_ELM_PREC(rec,k);
                        SET_ELM_PREC(rec,k,GET_ELM_PREC(rec,k+1));
                        SET_ELM_PREC(rec,k+1,tmp);
                    } else break;
                }
            }
        }
        /* Finally, we have to negate everything in the end: */
        for (j = save;j <= len;j++)
            SET_RNAM_PREC(rec,j,-(Int)(GET_RNAM_PREC(rec,j)));
    }
}

/****************************************************************************
**
*F * * * * * * * * * * * default functions for records  * * * * * * * * * * *
*/


void PrintPathPRec (
    Obj                 rec,
    Int                 indx )
{
    Pr( ".%H", (Int)NAME_RNAM( labs((Int)(GET_RNAM_PREC(rec,indx))) ), 0L );
}

/****************************************************************************
**
*F  FuncREC_NAMES( <self>, <rec> )  . . . . . . . .  record names of a record
**
**  'FuncREC_NAMES' implements a method for the operations 'RecNames'.
**
**  'RecNames( <rec> )'
**
**  'RecNames'  returns a list containing the  names of the components of the
**  record <rec> as strings.
*/
Obj InnerRecNames( Obj rec )
{
    Obj                 list;           /* list of record names, result    */
    UInt                rnam;           /* one name of record              */
    Obj                 string;         /* one name as string              */
    UInt                i;
    Obj                 name;
    SortPRecRNam(rec,0);   /* Make sure rnams are sorted and thus negative */

    /* allocate the list                                                   */
    list = NEW_PLIST( T_PLIST, LEN_PREC(rec) );
    SET_LEN_PLIST( list, LEN_PREC(rec) );

    /* loop over the components                                            */
    for ( i = 1; i <= LEN_PREC(rec); i++ ) {
        rnam = -(Int)(GET_RNAM_PREC( rec, i ));
        /* could have been moved by garbage collection */
        name = NAME_RNAM( rnam );
        string = CopyToStringRep( name );
        SET_ELM_PLIST( list, i, string );
        CHANGED_BAG( list );
    }

    /* return the list                                                     */
    return list;
}

Obj FuncREC_NAMES (
    Obj                 self,
    Obj                 rec )
{
    /* check the argument                                                  */
    switch (TNUM_OBJ(rec)) {
      case T_PREC:
      case T_PREC+IMMUTABLE:
        return InnerRecNames(rec);
#ifdef HPCGAP
      case T_AREC:
        return InnerRecNames(FromAtomicRecord(rec));
#endif
    }
    ErrorMayQuit("RecNames: <rec> must be a record (not a %s)",
                 (Int)TNAM_OBJ(rec), 0L);
    return Fail;
}


/****************************************************************************
**
*F  FuncREC_NAMES_COMOBJ( <self>, <rec> ) . . . record names of a record object
*/
/* same as FuncREC_NAMES except for different argument check  */
Obj FuncREC_NAMES_COMOBJ (
    Obj                 self,
    Obj                 rec )
{
    /* check the argument                                                  */
    switch (TNUM_OBJ(rec)) {
      case T_COMOBJ:
        return InnerRecNames(rec);
#ifdef HPCGAP
      case T_ACOMOBJ:
        return InnerRecNames(FromAtomicRecord(rec));
#endif
    }
    ErrorMayQuit("RecNames: <rec> must be a component object (not a %s)",
                 (Int)TNAM_OBJ(rec), 0L);
    return Fail;
}


/****************************************************************************
**
*F  EqPRec( <self>, <left>, <right> ) . . . . . . . comparison of two records
**
**  'EqPRec' returns '1L'  if the two  operands <left> and <right> are equal
**  and '0L' otherwise.  At least one operand must be a plain record.
*/
Int EqPRec(Obj left, Obj right)
{
    UInt                i;              /* loop variable                   */

    /* quick first checks                                                  */
    if ( ! IS_PREC(left) )
        return 0;
    if ( ! IS_PREC(right) )
        return 0;
    if ( LEN_PREC(left) != LEN_PREC(right) )
        return 0;

    /* ensure records are sorted by their RNam */
    SortPRecRNam(left,0);
    SortPRecRNam(right,0);

    CheckRecursionBefore();

    /* compare componentwise                                               */
    for ( i = 1; i <= LEN_PREC(right); i++ ) {

        /* compare the names                                               */
        if ( GET_RNAM_PREC(left,i) != GET_RNAM_PREC(right,i) ) {
            DecRecursionDepth();
            return 0;
        }

        /* compare the values                                              */
        if ( ! EQ(GET_ELM_PREC(left,i),GET_ELM_PREC(right,i)) ) {
            DecRecursionDepth();
            return 0;
        }
    }

    /* the records are equal                                               */
    DecRecursionDepth();
    return 1;
}


/****************************************************************************
**
*F  LtPRec( <self>, <left>, <right> ) . . . . . . . comparison of two records
**
**  'LtPRec' returns '1L'  if the operand  <left> is  less than the  operand
**  <right>, and '0L'  otherwise.  At least  one operand  must be a  plain
**  record.
*/
Int LtPRec(Obj left, Obj right)
{
    UInt                i;              /* loop variable                   */
    Int                 res;            /* result of comparison            */

    /* quick first checks                                                  */
    if ( ! IS_PREC(left) || ! IS_PREC(right) ) {
        if ( TNUM_OBJ(left ) < TNUM_OBJ(right) )  return 1;
        if ( TNUM_OBJ(left ) > TNUM_OBJ(right) )  return 0;
    }

    /* ensure records are sorted by their RNam */
    SortPRecRNam(left,0);
    SortPRecRNam(right,0);

    CheckRecursionBefore();
    res = 0;

    /* compare componentwise                                               */
    for ( i = 1; i <= LEN_PREC(right); i++ ) {

        /* if the left is a proper prefix of the right one                 */
        if ( LEN_PREC(left) < i ) {
            res = 1;
            break;
        }

        /* compare the names                                               */
        /* The sense of this comparison is determined by the rule that
           unbound entries compare less than bound ones                    */
        if ( GET_RNAM_PREC(left,i) != GET_RNAM_PREC(right,i) ) {
            res = !LT( NAME_RNAM( labs((Int)(GET_RNAM_PREC(left,i))) ),
                   NAME_RNAM( labs((Int)(GET_RNAM_PREC(right,i))) ) );
            break;
        }

        /* compare the values                                              */
        if ( ! EQ(GET_ELM_PREC(left,i),GET_ELM_PREC(right,i)) ) {
            res = LT( GET_ELM_PREC(left,i), GET_ELM_PREC(right,i) );
            break;
        }

    }

    /* the records are equal or the right is a prefix of the left          */
    DecRecursionDepth();
    return res;
}


/****************************************************************************
**
*F  SavePRec( <prec> )
**
*/

void SavePRec( Obj prec )
{
  UInt len,i;
  len = LEN_PREC(prec);
  SaveUInt(len);
  for (i = 1; i <= len; i++)
    {
      SaveUInt(GET_RNAM_PREC(prec, i));
      SaveSubObj(GET_ELM_PREC(prec, i));
    }
}

/****************************************************************************
**
*F  LoadPRec( <prec> )
**
*/

void LoadPRec( Obj prec )
{
  UInt len,i;
  len = LoadUInt();
  SET_LEN_PREC(prec,len);
  for (i = 1; i <= len; i++)
    {
      SET_RNAM_PREC(prec, i, LoadUInt());
      SET_ELM_PREC(prec, i, LoadSubObj());
    }
}

/****************************************************************************
**
*F  MarkPRecSubBags( <bag> ) . . . . marking function for precs and com. objs
**
**  'MarkPRecSubBags' is the marking function for bags of type 'T_PREC' or
**  'T_COMOBJ'.
*/
void MarkPRecSubBags(Obj bag)
{
    const Bag * data = CONST_PTR_BAG(bag);
    const UInt count = SIZE_BAG(bag) / sizeof(Bag);

    // while data[0] is unused for regular precords, it used during copying
    // to store a pointer to the copy; moreover, this mark function is also
    // used for component objects, which store their type in slot 0
    MarkBag(data[0]);

    for (UInt i = 3; i < count; i += 2) {
        MarkBag(data[i]);
    }
}


/****************************************************************************
**
*F * * * * * * * * * * * * * initialize module * * * * * * * * * * * * * * *
*/

/****************************************************************************
**
*V  BagNames  . . . . . . . . . . . . . . . . . . . . . . . list of bag names
*/
static StructBagNames BagNames[] = {
  { T_PREC,                     "record (plain)"            },
  { T_PREC +IMMUTABLE,          "record (plain,imm)"        },
  { -1,                         ""                          }
};


/****************************************************************************
**
*V  GVarFuncs . . . . . . . . . . . . . . . . . . list of functions to export
*/
static StructGVarFunc GVarFuncs [] = {

    GVAR_FUNC(REC_NAMES, 1, "rec"),
    GVAR_FUNC(REC_NAMES_COMOBJ, 1, "rec"),
    { 0, 0, 0, 0, 0 }

};


/****************************************************************************
**
*F  InitKernel( <module> )  . . . . . . . . initialise kernel data structures
*/
static Int InitKernel (
    StructInitInfo *    module )
{
    /* GASMAN marking functions and GASMAN names                           */
    InitBagNamesFromTable( BagNames );

    InitMarkFuncBags( T_PREC                     , MarkPRecSubBags );
    InitMarkFuncBags( T_PREC +IMMUTABLE          , MarkPRecSubBags );

#ifdef HPCGAP
    /* Immutable records are public                                        */
    MakeBagTypePublic( T_PREC +IMMUTABLE );
#endif

    /* init filters and functions                                          */
    InitHdlrFuncsFromTable( GVarFuncs );

    /* Install saving functions                                            */
    SaveObjFuncs[ T_PREC            ] = SavePRec;
    SaveObjFuncs[ T_PREC +IMMUTABLE ] = SavePRec;
    LoadObjFuncs[ T_PREC            ] = LoadPRec;
    LoadObjFuncs[ T_PREC +IMMUTABLE ] = LoadPRec;

    /* install into record function tables                                 */
    ElmRecFuncs[ T_PREC            ] = ElmPRec;
    ElmRecFuncs[ T_PREC +IMMUTABLE ] = ElmPRec;
    IsbRecFuncs[ T_PREC            ] = IsbPRec;
    IsbRecFuncs[ T_PREC +IMMUTABLE ] = IsbPRec;
    AssRecFuncs[ T_PREC            ] = AssPRec;
    AssRecFuncs[ T_PREC +IMMUTABLE ] = AssPRec;
    UnbRecFuncs[ T_PREC            ] = UnbPRec;
    UnbRecFuncs[ T_PREC +IMMUTABLE ] = UnbPRec;

    /* install tests for being copyable                                    */
    IsCopyableObjFuncs[ T_PREC            ] = AlwaysYes;
    IsCopyableObjFuncs[ T_PREC +IMMUTABLE ] = AlwaysYes;

#ifdef USE_THREADSAFE_COPYING
    SetTraversalMethod(T_PREC,            TRAVERSE_BY_FUNCTION, TraversePRecord, CopyPRecord);
    SetTraversalMethod(T_PREC +IMMUTABLE, TRAVERSE_BY_FUNCTION, TraversePRecord, CopyPRecord);
#else
    /* install into copy function tables                                  */
    CopyObjFuncs [ T_PREC                     ] = CopyPRec;
    CopyObjFuncs [ T_PREC +IMMUTABLE          ] = CopyPRec;
    CleanObjFuncs[ T_PREC                     ] = CleanPRec;
    CleanObjFuncs[ T_PREC +IMMUTABLE          ] = CleanPRec;
#endif

    /* install printer                                                     */
    PrintObjFuncs[  T_PREC            ] = PrintPRec;
    PrintObjFuncs[  T_PREC +IMMUTABLE ] = PrintPRec;
    PrintPathFuncs[ T_PREC            ] = PrintPathPRec;
    PrintPathFuncs[ T_PREC +IMMUTABLE ] = PrintPathPRec;

    // install the comparison methods
    for (UInt t1 = T_PREC; t1 <= T_PREC + IMMUTABLE; t1++) {
        for (UInt t2 = T_PREC; t2 <= T_PREC + IMMUTABLE; t2++) {
            EqFuncs[t1][t2] = EqPRec;
            LtFuncs[t1][t2] = LtPRec;
        }
    }

    /* install the type functions                                          */
    ImportGVarFromLibrary( "TYPE_PREC_MUTABLE",   &TYPE_PREC_MUTABLE   );
    ImportGVarFromLibrary( "TYPE_PREC_IMMUTABLE", &TYPE_PREC_IMMUTABLE );

    TypeObjFuncs[ T_PREC            ] = TypePRec;
    TypeObjFuncs[ T_PREC +IMMUTABLE ] = TypePRec;

    SetTypeObjFuncs[ T_PREC ] = SetTypePRecToComObj;

    MakeImmutableObjFuncs[ T_PREC   ] = MakeImmutablePRec;

    /* return success                                                      */
    return 0;
}


/****************************************************************************
**
*F  InitLibrary( <module> ) . . . . . . .  initialise library data structures
*/
static Int InitLibrary (
    StructInitInfo *    module )
{
    /* init filters and functions                                          */
    InitGVarFuncsFromTable( GVarFuncs );

    /* return success                                                      */
    return 0;
}


/****************************************************************************
**
*F  InitInfoPRecord() . . . . . . . . . . . . . . . . table of init functions
*/
static StructInitInfo module = {
    // init struct using C99 designated initializers; for a full list of
    // fields, please refer to the definition of StructInitInfo
    .type = MODULE_BUILTIN,
    .name = "precord",
    .initKernel = InitKernel,
    .initLibrary = InitLibrary,
};

StructInitInfo * InitInfoPRecord ( void )
{
    return &module;
}
