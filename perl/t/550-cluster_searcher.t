# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

use strict;
use warnings;

use Test::More;
use Time::HiRes qw( sleep );
use IO::Socket::INET;

my @ports = 7890 .. 7891;
BEGIN {
    if ( $^O =~ /(mswin|cygwin)/i ) {
        plan( 'skip_all', "fork on Windows not supported by Lucy" );
    }
    elsif ( $ENV{LUCY_VALGRIND} ) {
        plan( 'skip_all', "time outs cause probs under valgrind" );
    }
}

package SortSchema;
use base qw( Lucy::Plan::Schema );
use Lucy::Analysis::RegexTokenizer;

sub new {
    my $self       = shift->SUPER::new(@_);
    my $plain_type = Lucy::Plan::FullTextType->new(
        analyzer => Lucy::Analysis::RegexTokenizer->new );
    my $num_type = Lucy::Plan::Int32Type->new( sortable => 1, indexed => 0 );
    my $string_type = Lucy::Plan::StringType->new( sortable => 1 );
    $self->spec_field( name => 'content', type => $plain_type );
    $self->spec_field( name => 'number',  type => $num_type );
    $self->spec_field( name => 'port',    type => $string_type );
    return $self;
}

package main;

use Lucy::Test;
use LucyX::Remote::SearchServer;
use LucyX::Remote::ClusterSearcher;

my @kids;
my $number = 7;
for my $port (@ports) {
    my $kid = fork;
    if ($kid) {
        die "Failed fork: $!" unless defined $kid;
        push @kids, $kid;
    }
    else {
        my $folder  = Lucy::Store::RAMFolder->new;
        my $indexer = Lucy::Index::Indexer->new(
            index  => $folder,
            schema => SortSchema->new,
        );
        for (qw( a b c )) {
            my %doc = (
                content => "x $_ $port",
                number  => $number,
                port    => $port,
            );
            $indexer->add_doc(\%doc);
            $number += 2;
        }
        $indexer->commit;

        my $searcher = Lucy::Search::IndexSearcher->new( index => $folder );
        my $server = LucyX::Remote::SearchServer->new(
            port     => $port,
            searcher => $searcher,
            password => 'foo',
        );
        $server->serve;
        exit(0);
    }
}

# Allow time for the servers to set up their sockets.
sleep .25;

my $test_client_sock = IO::Socket::INET->new(
    PeerAddr => "localhost:$ports[0]",
    Proto    => 'tcp',
);
if ($test_client_sock) {
    plan( tests => 9 );
    undef $test_client_sock;
}
else {
    plan( 'skip_all', "Can't get a socket: $!" );
}

my $solo_cluster_searcher = LucyX::Remote::ClusterSearcher->new(
    schema   => SortSchema->new,
    shards   => ["localhost:$ports[0]"],
    password => 'foo',
);

is( $solo_cluster_searcher->doc_freq( field => 'content', term => 'x' ),
    3, "doc_freq" );
is( $solo_cluster_searcher->doc_max, 3, "doc_max" );
isa_ok( $solo_cluster_searcher->fetch_doc(1), "Lucy::Document::HitDoc", "fetch_doc" );
isa_ok( $solo_cluster_searcher->fetch_doc_vec(1),
    "Lucy::Index::DocVector", "fetch_doc_vec" );

my $hits = $solo_cluster_searcher->hits( query => 'x' );
is( $hits->total_hits, 3, "retrieved hits from search server" );

$hits = $solo_cluster_searcher->hits( query => 'a' );
is( $hits->total_hits, 1, "retrieved hit from search server" );

my $cluster_searcher = LucyX::Remote::ClusterSearcher->new(
    schema   => SortSchema->new,
    shards   => [ map {"localhost:$_"} @ports ],
    password => 'foo',
);

$hits = $cluster_searcher->hits( query => 'b' );
is( $hits->total_hits, 2, "matched hits across multiple shards" );

my %results;
$results{ $hits->next()->{content} } = 1;
$results{ $hits->next()->{content} } = 1;
my %expected = ( 'x b 7890' => 1, 'x b 7891' => 1, );

is_deeply( \%results, \%expected, "docs fetched from multiple shards" );

my $sort_spec = Lucy::Search::SortSpec->new(
    rules => [
        Lucy::Search::SortRule->new( field => 'number' ),
        Lucy::Search::SortRule->new( type  => 'doc_id' ),
    ],
);
$hits = $cluster_searcher->hits(
    query     => 'b',
    sort_spec => $sort_spec,
);
my @got;

while ( my $hit = $hits->next ) {
    push @got, $hit->{number};
}
$sort_spec = Lucy::Search::SortSpec->new(
    rules => [
        Lucy::Search::SortRule->new( field => 'number', reverse => 1 ),
        Lucy::Search::SortRule->new( type  => 'doc_id' ),
    ],
);
$hits = $cluster_searcher->hits(
    query     => 'b',
    sort_spec => $sort_spec,
);
my @reversed;
while ( my $hit = $hits->next ) {
    push @reversed, $hit->{number};
}
is_deeply(
    \@got,
    [ reverse @reversed ],
    "Sort hits accross multiple shards"
);

END {
    $solo_cluster_searcher->terminate if defined $solo_cluster_searcher;
    $cluster_searcher->terminate      if defined $cluster_searcher;
    for my $kid (@kids) {
        kill( TERM => $kid ) if $kid;
    }
}