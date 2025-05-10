/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 初始化一个指向RmRecord的指针（赋值其内部的data和size）
    // 3. 返回指向RmRecord的指针

    //0.加S锁
    //这里加S是因为之后还会调用update和delete，那里面会有exclusive上锁操作
    //但在seqscan里会遍历找get_record并判断条件，在这个过程中一直在获取行锁
    context->lock_mgr_->lock_IS_on_table(context->txn_,fd_);
    context->lock_mgr_->lock_shared_on_record(context->txn_,rid,fd_);
    //1
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    //2
    //std::unique_ptr<RmRecord> record_ptr = std::make_unique<RmRecord>(file_hdr_.record_size,page_handle.get_slot(rid.slot_no));
    //return record_ptr;
    return std::make_unique<RmRecord>(file_hdr_.record_size,page_handle.get_slot(rid.slot_no));
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char* buf, Context* context) {
    // Todo:
    // 1. 获取当前未满的page handle
    // 2. 在page handle中找到空闲slot位置
    // 3. 将buf复制到空闲slot位置
    // 4. 更新page_handle.page_hdr中的数据结构
    // 注意考虑插入一条记录后页面已满的情况，需要更新file_hdr_.first_free_page_no

    //0.加表锁
    context->lock_mgr_->lock_exclusive_on_table(context->txn_,fd_);
    //1
    RmPageHandle page_handle = create_page_handle();
    //2
    int first_free_slot_no = Bitmap::first_bit(0,page_handle.bitmap,file_hdr_.num_records_per_page);
    char* first_free_slot = page_handle.get_slot(first_free_slot_no);
    //3
    memcpy(first_free_slot, buf, file_hdr_.record_size);
    Bitmap::set(page_handle.bitmap,first_free_slot_no);
    //4
    page_handle.page_hdr->num_records++;
    if(page_handle.page_hdr->num_records >=file_hdr_.num_records_per_page){
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
        //page_handle.page_hdr->next_free_page_no = RM_NO_PAGE;
    }
    return Rid{page_handle.page->get_page_id().page_no,first_free_slot_no}; // 返回插入记录的rid
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
    //好像没被调用过
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    //每改变一次记录都要操作一次下面的操作
    char* slot = page_handle.get_slot(rid.slot_no);
    memcpy(slot, buf, file_hdr_.record_size);
    Bitmap::set(page_handle.bitmap,rid.slot_no);
    page_handle.page_hdr->num_records++;
    if(page_handle.page_hdr->num_records >=file_hdr_.num_records_per_page){
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
    }
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新page_handle.page_hdr中的数据结构
    // 注意考虑删除一条记录后页面未满的情况，需要调用release_page_handle()

    //0.加行锁
    context->lock_mgr_->lock_IX_on_table(context->txn_,fd_);
    context->lock_mgr_->lock_exclusive_on_record(context->txn_,rid,fd_);
    //1
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    //2
    Bitmap::reset(page_handle.bitmap,rid.slot_no);
    //3
    if(page_handle.page_hdr->num_records == file_hdr_.num_records_per_page){
        release_page_handle(page_handle);
    }
    page_handle.page_hdr->num_records--;//必须采用预判断的方式，虽然和下面被注释的逻辑一样但是test会报错
}

// void RmFileHandle::delete_record(const Rid& rid, Context* context) {
//     // Todo:
//     // 1. 获取指定记录所在的page handle
//     // 2. 更新page_handle.page_hdr中的数据结构
//     // 注意考虑删除一条记录后页面未满的情况，需要调用release_page_handle()

//     //0.加行锁
//     context->lock_mgr_->lock_IX_on_table(context->txn_,fd_);
//     context->lock_mgr_->lock_exclusive_on_record(context->txn_,rid,fd_);
//     //1
//     RmPageHandle page_handle = fetch_page_handle(rid.page_no);
//     //2
//     page_handle.page_hdr->num_records--;
//     Bitmap::reset(page_handle.bitmap,rid.slot_no);
//     //3
//     if(page_handle.page_hdr->num_records < file_hdr_.num_records_per_page){
//         release_page_handle(page_handle);
//     }
// }


/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新记录

    //0.加行锁
    context->lock_mgr_->lock_IX_on_table(context->txn_,fd_);
    context->lock_mgr_->lock_exclusive_on_record(context->txn_,rid,fd_);
    //1
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    //2
    char* slot = page_handle.get_slot(rid.slot_no);
    memcpy(slot, buf, file_hdr_.record_size);
    
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    // Todo:
    // 使用缓冲池获取指定页面，并生成page_handle返回给上层
    // if page_no is invalid, throw PageNotExistError exception
    PageId page_id;
    page_id.fd = fd_;
    page_id.page_no = page_no;
    if(page_no == INVALID_PAGE_ID){
        throw PageNotExistError("PageName||",page_no);//待解决
    }
    Page* fet_page = buffer_pool_manager_->fetch_page(page_id);
    RmPageHandle page_handle = RmPageHandle(&file_hdr_, fet_page);

    return page_handle;
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    // Todo:
    // 1.使用缓冲池来创建一个新page
    // 2.更新page handle中的相关信息
    // 3.更新file_hdr_

    //1
    PageId page_id;
    page_id.fd = fd_;
    Page* new_page = buffer_pool_manager_->new_page(&page_id);
    RmPageHandle page_handle = RmPageHandle(&file_hdr_,new_page);
    //2，更新page_handle三部分
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;//
    page_handle.page_hdr->num_records = 0;//
    Bitmap::init(page_handle.bitmap,file_hdr_.bitmap_size);
    //3 file_hdr的变量只有num_pages和first_free_page_no会变
    file_hdr_.num_pages++;
    file_hdr_.first_free_page_no = new_page->get_page_id().page_no;
    return page_handle;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    // Todo:
    // 1. 判断file_hdr_中是否还有空闲页
    //     1.1 没有空闲页：使用缓冲池来创建一个新page；可直接调用create_new_page_handle()
    //     1.2 有空闲页：直接获取第一个空闲页
    // 2. 生成page handle并返回给上层

    //1
    if(file_hdr_.first_free_page_no == RM_NO_PAGE){
        return create_new_page_handle();
    }
    else{
        //2
        return fetch_page_handle(file_hdr_.first_free_page_no);
    } 
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle&page_handle) {
    // Todo:
    // 当page从已满变成未满，考虑如何更新：
    // 1. page_handle.page_hdr->next_free_page_no
    // 2. file_hdr_.first_free_page_no
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
    file_hdr_.first_free_page_no = page_handle.page->get_page_id().page_no;
}