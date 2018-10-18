/*
 *                         Vortex OpenSplice
 *
 *   This software and documentation are Copyright 2006 to TO_YEAR ADLINK
 *   Technology Limited, its affiliated companies and licensors. All rights
 *   reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#include "v__partitionAdmin.h"
#include "v_kernel.h"
#include "v_entity.h"

#include "v__policy.h"

#include "vortex_os.h"

#define CLASSNAME "kernelModuleI::v_partitionAdmin"

static c_bool
checkPartitionInterest(
    c_object o,
    c_voidp arg)
{
    v_partitionInterest partitionInterest = (v_partitionInterest)o;
    v_partition partition = v_partition(arg);
    c_bool result = TRUE;

    if (v_partitionStringMatchesExpression(v_partitionName(partition), partitionInterest->expression)) {
        result = FALSE;
    }

    return result;
}

struct resolvePartitionsArg {
    v_kernel kernel;
    c_iter   *partitions;
};

static c_bool
resolvePartitions(
    c_object o,
    c_voidp arg)
{
    v_partitionInterest di = v_partitionInterest(o);
    struct resolvePartitionsArg *a = (struct resolvePartitionsArg *)arg;
    c_iter list;
    v_partition d;

    list = v_resolvePartitions(a->kernel, di->expression);
    d = v_partition(c_iterTakeFirst(list));
    while (d != NULL) {
        *(a->partitions) = c_iterInsert(*(a->partitions), d);
        /* The reference is transferred from list to a->partitions,
         * so no c_free(d).
         */
        d = v_partition(c_iterTakeFirst(list));
    }
    c_iterFree(list);

    return TRUE; /* never break the walk */
}

struct updatePartitionsArg {
    c_iter *addPartitions;
    c_iter *removePartitions;
};
#include "v_entity.h"
static c_bool
updatePartitions(
    c_object o,
    c_voidp arg)
{
    v_partition d = v_partition(o);
    struct updatePartitionsArg *a = (struct updatePartitionsArg *)arg;

    if (c_iterContains(*(a->addPartitions), (void *)d)) {
        c_iterTake(*(a->addPartitions), (void *)d);
        /* remove since we already know the partition */
        c_free(d);

    } else {
        /* add to remove list, we are no longer interested */
        *(a->removePartitions) = c_iterInsert(*(a->removePartitions), c_keep(d));
    }

    return TRUE; /* never break the walk */
}

static void
removePartition(
    void *o,
    c_iterActionArg arg)
{
    v_partition p = v_partition(o);
    c_table t = (c_table)arg;
    v_partition found;

    found = c_tableRemove(t, p, NULL, NULL);
    /* 'found' might be NULL, since partitions are only added when to this
     * administration when a group exists for that partition
     */
    c_free(found);
}

static void
addPartition(
    void *o,
    c_iterActionArg arg)
{
    v_partition p = v_partition(o);
    c_table t = (c_table)arg;
    v_partition found;

    found = c_tableInsert(t, p);
    if (found != p) {
        c_free(found);
    }
}

v_partitionAdmin
v_partitionAdminNew(
    v_kernel kernel)
{
    v_partitionAdmin da;

    assert(C_TYPECHECK(kernel,v_kernel));

    da = v_partitionAdmin(v_objectNew(kernel, K_DOMAINADMIN));
    if (da != NULL) {
        da->partitions         = c_tableNew(v_kernelType(kernel, K_DOMAIN),"name");
        da->partitionInterests = c_tableNew(v_kernelType(kernel, K_DOMAININTEREST), "expression");

        if ((da->partitions == NULL) || (da->partitionInterests == NULL) ||
            c_mutexInit(c_getBase(da), &da->mutex) != SYNC_RESULT_SUCCESS) {
            c_free(da);
            da = NULL;
        }
    }
    return da;
}

void
v_partitionAdminFree(
    v_partitionAdmin da)
{
    assert(C_TYPECHECK(da,v_partitionAdmin));

    if (da != NULL) {
        c_free(da);
    }
}

void
v_partitionAdminFill(
    v_partitionAdmin da,
    const c_char *partitionExpr)
{
    /* partitionExpr: expression or absolute partition name */
    c_iter partitions;
    v_partition d;

    partitions = NULL;
    c_mutexLock(&da->mutex);
    if (v_partitionExpressionIsAbsolute(partitionExpr)) {
        d = v_partitionNew(v_objectKernel(da), partitionExpr, NULL);
        (void)ospl_c_insert(da->partitions, d);
        c_free(d);
    } else {
        v_partitionInterest pi, found;
        pi = v_partitionInterestNew(v_objectKernel(da), partitionExpr);
        found = c_tableInsert(da->partitionInterests, pi);
        if (found == pi) {
            partitions = v_resolvePartitions(v_objectKernel(da), partitionExpr);
            while ((d = c_iterTakeFirst(partitions)) != NULL) {
                (void)ospl_c_insert(da->partitions, d);
                c_free(d);
            }
            c_iterFree(partitions);
        } /* else expression already member */
        c_free(pi);
    }
    c_mutexUnlock(&da->mutex);
}

c_bool
v_partitionAdminAdd(
    v_partitionAdmin da,
    v_partition d)
{
    c_bool inserted;
    c_mutexLock(&da->mutex);
    inserted = !c_tableWalk(da->partitionInterests, checkPartitionInterest, d);
    if (inserted) {
        inserted = (d == ospl_c_insert(da->partitions, d));
    }
    c_mutexUnlock(&da->mutex);
    return inserted;
}

c_bool
v_partitionAdminUpdate(
    v_partitionAdmin da,
    v_partitionPolicyI partitionExpr,
    c_iter *addedPartitions,
    c_iter *removedPartitions)
{
    c_iter                   dexpressions;    /* iterator of partition expressions */
    c_char                   *dexpr;          /* partition expression */
    v_partitionInterest         di;
    struct resolvePartitionsArg resolveArg;
    struct updatePartitionsArg  updateArg;

    assert(C_TYPECHECK(da, v_partitionAdmin));
    assert(removedPartitions != NULL);
    assert(addedPartitions != NULL);

    *removedPartitions = NULL;
    *addedPartitions = NULL;

    resolveArg.kernel = v_objectKernel(da);

    c_mutexLock(&da->mutex);
    /* The absolute partition names will be added at the end of
     * the algorithm.
     * The partition expressions in the parameter of partitionExpr,
     * replace the existing in da->partitionInterests.
     */
    c_free(da->partitionInterests);
    da->partitionInterests = c_tableNew(v_kernelType(resolveArg.kernel, K_DOMAININTEREST),
                              "expression");
    assert(c_count(da->partitionInterests) == 0);
    dexpressions = v_partitionPolicySplit(partitionExpr);
    if (dexpressions == NULL) {
        /* switch to default */
        *addedPartitions = c_iterInsert(*addedPartitions, v_partitionNew(resolveArg.kernel, "", NULL));
    } else {
        dexpr = (c_char *)c_iterTakeFirst(dexpressions);
        while (dexpr != NULL) {
            if (v_partitionExpressionIsAbsolute(dexpr)) {
                *addedPartitions = c_iterInsert(*addedPartitions,
                    v_partitionNew(resolveArg.kernel, dexpr, NULL));
                 /* ref transferred to addedPartitions */
            } else {
                di = v_partitionInterestNew(resolveArg.kernel, (const c_char *)dexpr);
                c_tableInsert(da->partitionInterests, di);
                c_free(di);
            }
            os_free(dexpr);
            dexpr = (c_char *)c_iterTakeFirst(dexpressions);
        }
        c_iterFree(dexpressions);
    }

    /* The given expressions are now divided across
     * 'addedpartitions' and 'da->partitionInterests'.
     * Now first add partitions to 'addedpartitions' that fit the
     * expressions in 'da->partitionInterests'.
     */
    resolveArg.partitions = addedPartitions;
    c_tableWalk(da->partitionInterests, resolvePartitions, (c_voidp)&resolveArg);

    /* Now 'addedpartitions' contains all partitions to be added
     * by the publisher/subscriber.
     * 'da->partitions' contains the old set of partitions.
     * We must check whether those partitions must remain in
     * the set or must be removed.
     * For every partition in 'da->partitions' do
     *    if partition in 'addedpartitions' then remove from 'addedpartitions'
     *    else add to 'removedpartitions'
     * For every partition in 'removedpartitions' remove from 'da->partitions'.
     */
    updateArg.addPartitions = addedPartitions;
    updateArg.removePartitions = removedPartitions;
    (void)c_tableWalk(da->partitions, updatePartitions, (c_voidp)&updateArg);

    c_iterWalk(*removedPartitions, removePartition, (c_voidp)da->partitions);

    /* The da->partitions now contains partitions that still comply to new
     * partitionPolicy. So all partitions in added partitions, must be added
     * to da->partitions, so it reflects all connected partitions.
     */
    c_iterWalk(*addedPartitions, addPartition, (c_voidp)da->partitions);
    c_mutexUnlock(&da->mutex);

    return TRUE;
}

c_iter
v_partitionAdminLookup(
    v_partitionAdmin da,
    const c_char *partitionExpr)
{
    c_iter list;
    c_collection q;
    q_expr expr;
    c_value params[1];

    assert(da != NULL);
    assert(C_TYPECHECK(da,v_partitionAdmin));

    expr = (q_expr)q_parse("name like %0");
    params[0] = c_stringValue((c_char *)partitionExpr);
    c_mutexLock(&da->mutex);
    q = c_queryNew(da->partitions,expr,params);
    list = ospl_c_select(q,0);
    c_mutexUnlock(&da->mutex);
    q_dispose(expr);
    c_free(q);

    return list;
}

c_bool
v_partitionAdminWalk(
    v_partitionAdmin da,
    c_action action,
    c_voidp arg)
{
    c_bool result;

    c_mutexLock(&da->mutex);
    result = c_tableWalk(da->partitions, action, arg);
    c_mutexUnlock(&da->mutex);

    return result;
}

c_ulong
v_partitionAdminCount(
    v_partitionAdmin _this)
{
    c_ulong result;

    c_mutexLock(&_this->mutex);
    result = c_count(_this->partitions);
    c_mutexUnlock(&_this->mutex);

    return result;
}
