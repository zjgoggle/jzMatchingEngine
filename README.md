# Jz Matching Engine

A simplified efficient matching engine that processes client NewOrder & CancelOrder and generates Trade events and OrderFill responses.

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


***This program was built by g++ version 13.1.0 on ubuntu 22.04***