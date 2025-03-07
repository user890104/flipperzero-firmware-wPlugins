/**
 * @file variable_item_list.h
 * GUI: VariableItemList view module API
 */

#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VariableItemList VariableItemList;
typedef struct VariableItem VariableItem;
typedef void (*VariableItemChangeCallback)(VariableItem* item);
typedef void (*VariableItemListEnterCallback)(void* context, uint32_t index);

/** Allocate and initialize VariableItemList
 *
 * @return     VariableItemList*
 */
VariableItemList* variable_item_list_alloc(void);

/** Deinitialize and free VariableItemList
 *
 * @param      variable_item_list  VariableItemList instance
 */
void variable_item_list_free(VariableItemList* variable_item_list);

/** Clear all elements from list
 *
 * @param      variable_item_list  VariableItemList instance
 */
void variable_item_list_reset(VariableItemList* variable_item_list);

/** Get VariableItemList View instance
 *
 * @param      variable_item_list  VariableItemList instance
 *
 * @return     View instance
 */
View* variable_item_list_get_view(VariableItemList* variable_item_list);

/** Add item to VariableItemList
 *
 * @param      variable_item_list  VariableItemList instance
 * @param      label               item name
 * @param      values_count        item values count
 * @param      change_callback     called on value change in gui
 * @param      context             item context
 *
 * @return     VariableItem* item instance
 */
VariableItem* variable_item_list_add(
    VariableItemList* variable_item_list,
    const char* label,
    uint8_t values_count,
    VariableItemChangeCallback change_callback,
    void* context);

/** Get pre-existing item instance in VariableItemList
 *
 * @param      variable_item_list  VariableItemList instance
 * @param      position            index of the item instance to get
 *
 * @return     VariableItem* item instance
 */
VariableItem* variable_item_list_get(VariableItemList* variable_item_list, uint8_t position);

/** Set enter callback
 *
 * @param      variable_item_list  VariableItemList instance
 * @param      callback            VariableItemListEnterCallback instance
 * @param      context             pointer to context
 */
void variable_item_list_set_enter_callback(
    VariableItemList* variable_item_list,
    VariableItemListEnterCallback callback,
    void* context);

void variable_item_list_set_selected_item(VariableItemList* variable_item_list, uint8_t index);

uint8_t variable_item_list_get_selected_item_index(VariableItemList* variable_item_list);

/** Set optional header for variable item list
 * Must be called before adding items OR after adding items and before set_selected_item()
 *
 * @param      variable_item_list  VariableItemList instance
 * @param      header              header to set
 */
void variable_item_list_set_header(VariableItemList* variable_item_list, const char* header);

/** Set item current selected index
 *
 * @param      item                 VariableItem* instance
 * @param      current_value_index  The current value index
 */
void variable_item_set_current_value_index(VariableItem* item, uint8_t current_value_index);

/** Set number of values for item
 *
 * @param      item                 VariableItem* instance
 * @param      values_count         The new values count
 */
void variable_item_set_values_count(VariableItem* item, uint8_t values_count);

/** Set new label for item
 *
 * @param      item                 VariableItem* instance
 * @param      label                The new label text
 */
void variable_item_set_item_label(VariableItem* item, const char* label);

/** Set item current selected text
 *
 * @param      item                VariableItem* instance
 * @param      current_value_text  The current value text
 */
void variable_item_set_current_value_text(VariableItem* item, const char* current_value_text);

/** Set item locked state and text
 * Locked message is saved, you can set_locked(item, false, "Message") and then set_locked(item, true, NULL), it will use previous locked message
 *
 * @param      item                VariableItem* instance
 * @param      locked              Is item locked boolean
 * @param      locked_message      The locked message text
 */
void variable_item_set_locked(VariableItem* item, bool locked, const char* locked_message);

/** Get item current selected index
 *
 * @param      item  VariableItem* instance
 *
 * @return     uint8_t current selected index
 */
uint8_t variable_item_get_current_value_index(VariableItem* item);

/** Get item context
 *
 * @param      item  VariableItem* instance
 *
 * @return     void* item context
 */
void* variable_item_get_context(VariableItem* item);

#ifdef __cplusplus
}
#endif