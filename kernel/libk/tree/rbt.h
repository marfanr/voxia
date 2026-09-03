#ifndef __LIBK__TREE__RBT__
#define __LIBK__TREE__RBT__

#ifndef RBT_TYPE
struct __rbt_type {
	unsigned long id; 
};
#define RBT_TYPE struct __rbt_type
#endif 

#ifndef RBT_ID_NAME
#define RBT_ID_NAME id
#endif 

#define FIELD_ACCESS(data_ptr, field) ((data_ptr)->field)

#include <libk/serial.h>
#include <str.h>
#include <type.h>

typedef enum { RBT_RED, RBT_BLACK } rbt_node_color;

typedef struct rbt_node rbt_node;
struct rbt_node {
	RBT_TYPE* data;
	rbt_node* left;
	rbt_node* right;
	rbt_node* parent;
	rbt_node_color color;
};

static inline void
rbt_rotate_left(rbt_node** root, rbt_node* x, rbt_node* NIL) {
	rbt_node* y = x->right;
	x->right = y->left;

	if (y->left != NIL)
		y->left->parent = x;

	y->parent = x->parent;
	if (x->parent == NIL)
		*root = y;
	else if (x == x->parent->left)
		x->parent->left = y;
	else
		x->parent->right = y;
	y->left = x;
	x->parent = y;
}

static inline void
rbt_rotate_right(rbt_node** root, rbt_node* y, rbt_node* NIL) {
	rbt_node* x = y->left;
	y->left = x->right;

	if (x->right != NIL) {
		x->right->parent = y;
	}
	x->parent = y->parent;
	if (y->parent == NIL) {
		*root = x;
	} else if (y == y->parent->right) {
		y->parent->right = x;
	} else {
		y->parent->left = x;
	}
	x->right = y;
	y->parent = x;
}

static inline void rbt_fix_insert(rbt_node** root, rbt_node* y, rbt_node* NIL) {
	while (y->parent->color == RBT_RED) {
		if (y->parent == y->parent->parent->left) {
			rbt_node* x = y->parent->parent->right;
			if (x != NIL && x->color == RBT_RED) {
				y->parent->color = RBT_BLACK;
				x->color = RBT_BLACK;
				y->parent->parent->color = RBT_RED;
				y = y->parent->parent;
			} else {
				if (y == y->parent->right) {
					y = y->parent;
					rbt_rotate_left(root, y, NIL);
				}
				y->parent->color = RBT_BLACK;
				y->parent->parent->color = RBT_RED;
				rbt_rotate_right(root, y->parent->parent, NIL);
			}
		} else {
			rbt_node* x = y->parent->parent->left;
			if (x != NIL && x->color == RBT_RED) {
				y->parent->color = RBT_BLACK;
				x->color = RBT_BLACK;
				y->parent->parent->color = RBT_RED;
				y = y->parent->parent;
			} else {
				if (y == y->parent->left) {
					y = y->parent;
					rbt_rotate_right(root, y, NIL);
				}
				y->parent->color = RBT_BLACK;
				y->parent->parent->color = RBT_RED;
				rbt_rotate_left(root, y->parent->parent, NIL);
			}
		}
	}
	(*root)->color = RBT_BLACK;
}

static inline int
rbt_insert_node(rbt_node** root, rbt_node* z, RBT_TYPE* data, rbt_node* NIL) {
	memset(z, 0, sizeof(rbt_node));
	z->data = data;
	z->left = z->right = NIL;
	z->color = RBT_RED;

	rbt_node* parent = NIL;
	rbt_node* curent = *root;
	while (curent != NIL) {
		parent = curent;
		if (FIELD_ACCESS(z->data, RBT_ID_NAME) < FIELD_ACCESS(curent->data, RBT_ID_NAME))
			curent = curent->left;
		else
			curent = curent->right;
	}

	z->parent = parent;
	if (parent == NIL) {
		*root = z;
	} else if (FIELD_ACCESS(z->data, RBT_ID_NAME) < FIELD_ACCESS(parent->data, RBT_ID_NAME))
		parent->left = z;
	else
		parent->right = z;

	if (z->parent == NIL) {
		z->color = RBT_BLACK;
		return 1;
	}

	if (z->parent->parent == NIL) {
		return 1;
	}

	rbt_fix_insert(root, z, NIL);
	return 1;
}

static inline rbt_node*
rbt_search_node(rbt_node* root, uint64_t id, rbt_node* NIL) {
	rbt_node* curr = root;
	while (curr != NIL && curr->data != NULL && FIELD_ACCESS(curr->data, RBT_ID_NAME) != id) {
		if (id < FIELD_ACCESS(curr->data, RBT_ID_NAME))
			curr = curr->left;
		else
			curr = curr->right;
	}
	return curr;
}

static inline void rbt_fix_delete(rbt_node** root, rbt_node* x, rbt_node* x_parent, rbt_node* NIL) {
	while (x != *root && x->color == RBT_BLACK) {
		if (x == x_parent->left) {
			rbt_node* w = x_parent->right;
			if (w->color == RBT_RED) {
				w->color = RBT_BLACK;
				x_parent->color = RBT_RED;
				rbt_rotate_left(root, x_parent, NIL);
				w = x_parent->right;
			}
			if (w->left->color == RBT_BLACK && w->right->color == RBT_BLACK) {
				w->color = RBT_RED;
				x = x_parent;
				x_parent = x->parent;
			} else {
				if (w->right->color == RBT_BLACK) {
					w->left->color = RBT_BLACK;
					w->color = RBT_RED;
					rbt_rotate_right(root, w, NIL);
					w = x_parent->right;
				}
				w->color = x_parent->color;
				x_parent->color = RBT_BLACK;
				w->right->color = RBT_BLACK;
				rbt_rotate_left(root, x_parent, NIL);
				x = *root;
			}
		} else {
			rbt_node* w = x_parent->left;
			if (w->color == RBT_RED) {
				w->color = RBT_BLACK;
				x_parent->color = RBT_RED;
				rbt_rotate_right(root, x_parent, NIL);
				w = x_parent->left;
			}
			if (w->right->color == RBT_BLACK && w->left->color == RBT_BLACK) {
				w->color = RBT_RED;
				x = x_parent;
				x_parent = x->parent;
			} else {
				if (w->left->color == RBT_BLACK) {
					w->right->color = RBT_BLACK;
					w->color = RBT_RED;
					rbt_rotate_left(root, w, NIL);
					w = x_parent->left;
				}
				w->color = x_parent->color;
				x_parent->color = RBT_BLACK;
				w->left->color = RBT_BLACK;
				rbt_rotate_right(root, x_parent, NIL);
				x = *root;
			}
		}
	}
	x->color = RBT_BLACK;
}

static inline void
rbt_remove_node(rbt_node** root, rbt_node* z, rbt_node* NIL) {
	rbt_node* y = z;
	rbt_node* x;
	rbt_node* x_parent;
	rbt_node_color y_original_color = y->color;

	if (z->left == NIL) {
		x = z->right;
		x_parent = z->parent;
		if (z->parent == NIL) {
			*root = x;
		} else if (z == z->parent->left) {
			z->parent->left = x;
		} else {
			z->parent->right = x;
		}
		if (x != NIL) {
			x->parent = z->parent;
		}
	} else if (z->right == NIL) {
		x = z->left;
		x_parent = z->parent;
		if (z->parent == NIL) {
			*root = x;
		} else if (z == z->parent->left) {
			z->parent->left = x;
		} else {
			z->parent->right = x;
		}
		if (x != NIL) {
			x->parent = z->parent;
		}
	} else {
		y = z->right;
		while (y->left != NIL) {
			y = y->left;
		}
		y_original_color = y->color;
		x = y->right;
		
		if (y->parent == z) {
			x_parent = y;
		} else {
			x_parent = y->parent;
			x_parent->left = y->right;
			if (y->right != NIL) {
				y->right->parent = x_parent;
			}
			y->right = z->right;
			if (y->right != NIL) {
				y->right->parent = y;
			}
		}

		if (z->parent == NIL) {
			*root = y;
		} else if (z == z->parent->left) {
			z->parent->left = y;
		} else {
			z->parent->right = y;
		}
		y->parent = z->parent;
		y->left = z->left;
		if (y->left != NIL) {
			y->left->parent = y;
		}
		y->color = z->color;
	}
	
	if (y_original_color == RBT_BLACK) {
		rbt_fix_delete(root, x, x_parent, NIL);
	}
}

#endif 
