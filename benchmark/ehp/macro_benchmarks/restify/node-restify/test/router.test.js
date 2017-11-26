// Copyright 2012 Mark Cavage, Inc.  All rights reserved.

'use strict';
/* eslint-disable func-names */

var restify = require('../lib');
var clients = require('restify-clients');

if (require.cache[__dirname + '/lib/helper.js']) {
    delete require.cache[__dirname + '/lib/helper.js'];
}
var helper = require('./lib/helper.js');

///--- Globals

var test = helper.test;
var mockResponse = function respond(req, res, next) {
    res.send(200);
};

///--- Tests

test('render route', function(t) {
    var server = restify.createServer();
    server.get({ name: 'countries', path: '/countries' }, mockResponse);
    server.get({ name: 'country', path: '/countries/:name' }, mockResponse);
    server.get(
        { name: 'cities', path: '/countries/:name/states/:state/cities' },
        mockResponse
    );

    var countries = server.router.render('countries', {});
    t.equal(countries, '/countries');

    var country = server.router.render('country', { name: 'Australia' });
    t.equal(country, '/countries/Australia');

    var cities = server.router.render('cities', {
        name: 'Australia',
        state: 'New South Wales'
    });
    t.equal(cities, '/countries/Australia/states/New%20South%20Wales/cities');

    t.end();
});

test('render route (missing params)', function(t) {
    var server = restify.createServer();
    server.get(
        { name: 'cities', path: '/countries/:name/states/:state/cities' },
        mockResponse
    );

    try {
        server.router.render('cities', { name: 'Australia' });
    } catch (ex) {
        t.equal(ex, 'Error: Route <cities> is missing parameter <state>');
    }

    t.end();
});

test('GH #704: render route (special charaters)', function(t) {
    var server = restify.createServer();
    server.get({ name: 'my-route', path: '/countries/:name' }, mockResponse);

    var link = server.router.render('my-route', { name: 'Australia' });
    t.equal(link, '/countries/Australia');

    t.end();
});

test('GH #704: render route (with sub-regex param)', function(t) {
    var server = restify.createServer();
    server.get(
        {
            name: 'my-route',
            path: '/countries/:code([A-Z]{2,3})'
        },
        mockResponse
    );

    var link = server.router.render('my-route', { code: 'FR' });
    t.equal(link, '/countries/FR');

    link = server.router.render('my-route', { code: '111' });
    t.equal(link, '/countries/111');
    t.end();
});

test('GH-796: render route (with multiple sub-regex param)', function(t) {
    var server = restify.createServer();
    server.get(
        {
            name: 'my-route',
            path: '/countries/:code([A-Z]{2,3})/:area([0-9]+)'
        },
        mockResponse
    );

    var link = server.router.render('my-route', { code: '111', area: 42 });
    t.equal(link, '/countries/111/42');
    t.end();
});

test('render route (with encode)', function(t) {
    var server = restify.createServer();
    server.get({ name: 'my-route', path: '/countries/:name' }, mockResponse);

    var link = server.router.render('my-route', { name: 'Trinidad & Tobago' });
    t.equal(link, '/countries/Trinidad%20%26%20Tobago');

    t.end();
});

test('render route (query string)', function(t) {
    var server = restify.createServer();
    server.get({ name: 'country', path: '/countries/:name' }, mockResponse);

    var country1 = server.router.render(
        'country',
        {
            name: 'Australia'
        },
        {
            state: 'New South Wales',
            'cities/towns': 5
        }
    );

    t.equal(
        country1,
        '/countries/Australia?state=New%20South%20Wales&cities%2Ftowns=5'
    );

    var country2 = server.router.render(
        'country',
        {
            name: 'Australia'
        },
        {
            state: 'NSW & VIC',
            'cities&towns': 5
        }
    );

    t.equal(
        country2,
        '/countries/Australia?state=NSW%20%26%20VIC&cities%26towns=5'
    );

    t.end();
});

test('clean up xss for 404', function(t) {
    var server = restify.createServer();

    server.listen(3000, function(listenErr) {
        t.ifError(listenErr);

        var client = clients.createStringClient({
            url: 'http://127.0.0.1:3000/'
        });

        client.get(
            {
                path:
                    '/no5_such3_file7.pl?%22%3E%3Cscript%3Ealert(73541);%3C/' +
                    'script%3E',
                headers: {
                    connection: 'close'
                }
            },
            function(clientErr, req, res, data) {
                t.ok(clientErr);
                t.ok(
                    data.indexOf('%22%3E%3Cscript%3Ealert(73541)') === -1,
                    'should not reflect raw url'
                );

                server.close(function() {
                    t.end();
                });
            }
        );
    });
});

test('Strict routing handles root path', function(t) {
    var server = restify.createServer({ strictRouting: true });
    function noop() {}
    server.get('/', noop);

    var root = server.router.routes.GET[0];
    t.ok(root.path.test('/'));

    t.end();
});

test('Strict routing distinguishes trailing slash', function(t) {
    var server = restify.createServer({ strictRouting: true });
    function noop() {}

    server.get('/trailing/', noop);
    server.get('/no-trailing', noop);

    var trailing = server.router.routes.GET[0];
    t.ok(trailing.path.test('/trailing/'));
    t.notOk(trailing.path.test('/trailing'));

    var noTrailing = server.router.routes.GET[1];
    t.ok(noTrailing.path.test('/no-trailing'));
    t.notOk(noTrailing.path.test('/no-trailing/'));

    t.end();
});

test('Default non-strict routing ignores trailing slash(es)', function(t) {
    var server = restify.createServer();
    function noop() {}

    server.get('/trailing/', noop);
    server.get('/no-trailing', noop);

    var trailing = server.router.routes.GET[0];
    t.ok(trailing.path.test('/trailing/'));
    t.ok(trailing.path.test('//trailing//'));
    t.ok(trailing.path.test('/trailing'));

    var noTrailing = server.router.routes.GET[1];
    t.ok(noTrailing.path.test('/no-trailing'));
    t.ok(noTrailing.path.test('//no-trailing//'));
    t.ok(noTrailing.path.test('/no-trailing/'));

    t.end();
});

test('Find existing route with path', function(t) {
    var server = restify.createServer();
    function noop() {}

    var routePath = '/route/:withParam';
    server.get(routePath, noop);

    var foundRoute = server.router.findByPath(
        '/route/:withADifferentParamName',
        { method: 'GET' }
    );
    t.equal(foundRoute.spec.path, routePath);

    var notFoundRoute = server.router.findByPath(
        '/route/:withADifferentParamName([A-Z]{2,3})',
        { method: 'GET' }
    );
    t.notOk(notFoundRoute);

    t.end();
});
