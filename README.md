# Jz Matching Engine

[![Build](https://github.com/zjgoggle/jzMatchingEngine/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/zjgoggle/jzMatchingEngine/actions/workflows/cmake-multi-platform.yml)
[![License](https://img.shields.io/badge/license-MIT-blue)](https://github.com/zjgoggle/jzMatchingEngine/blob/main/LICENSE-MIT)

A simplified efficient matching engine that processes client NewOrder & CancelOrder and generates trade events.

## Requirements

* Receive requests from stdin, each line representing a request.
  - AddOrderRequest: msgtype, orderid, side, quantity, price (e.g., 0,123,0,9,1000)
    - msgtype: 0
    - orderid: unique positive integer to identify each order; used to reference existing orders for cancel and fill messages
    - side: 0 (Buy), 1 (Sell)
    - quantity: maximum quantity to buy/sell (positive integer)
    - price: max price at which to buy/min price to sell (decimal number)
  
  - CancelOrderRequest: msgtype, orderid (e.g., 1,123)
    - msgtype: 1
    - orderid: ID of the order to remove
* Matching engine generates Trade events or Fill responses, each line representing an event/response. Every pair of orders that matches generates a TradeEvent. If an aggressive order has enough quantity to match multiple resting orders, a TradeEvent is
output for each match.
  - TradeEvent: msgtype, quantity, price (e.g. 2,2,1025)
    - msgtype: 2
    - quantity: amount that traded (positive integer)
    - price: indicating the price at which the trade happened (decimal number)
  - OrderFullyFilled: msgtype, orderid (e.g., 3,123)
    - msgtype: 3
    - orderid: ID of the order that was removed
  - OrderPartiallyFilled: msgtype, orderid, quantity (e.g., 4,123,3)
    - msgtype: 4
    - orderid: ID of the order to modify
    - quantity: the new quantity of the modified order (positive integer)

* Errors are printed to stderr.

## Design

* Use a vector as object pool to reduce memory allocation. It keeps all objects in contiguous memory which has better memory locality.
  
* Each OrderBook has 3 data structures.
  - Price OrderList: a list of orders that belongs to a price level.
  - By-price HashMap<Price, OrderList>: fast find the orders for a price level.
  - min/max Price Heap of price levels(Price, PointerToOrderList): fast check/remove top price and insert a price.
  - OrderID HashMap<OrderID, OrderKey>: fast find an order in a book. OrderKey contains a pointer to OrderList and a poniter to by-price HashMap.

* Time complexities:
  - Find top price to match: O(1) to find the opt of Price Heap.
  - Removing top price takes O(log(N)).
  - Add a new order to order book.
    - If price is already in orderbook, it takes O(1)  time to look up By-price HashMapp and append the order to OrderList.
    - Else, it looks O(log(N)) time to push into Price Heap.
  - Cancel an order: O(1) to look up OrderID HashMap and remove it from OrderList.
    - when there's no order for a price, remove it if it's top price; else, keep the empty list until it becomes the top price.

***This program was built by g++ version 13.1.0 on ubuntu 22.04***